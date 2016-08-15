#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <list>

#include "api.h"
#include "network_ip.h"

#ifdef PARALLEL //Parallel
#include "parallel.h"
#endif //Parallel

#include "phy_lte.h"
#include "lte_harq.h"
#include "layer2_lte_sch.h"
#include "layer2_lte_sch_roundrobin.h"
#include "layer2_lte_sch_pf.h"
#include "layer2_lte_sch_ue_default.h"
#include "layer2_lte_rlc.h"

#include "layer2_lte_establishment.h"
#include "layer2_lte.h"
#include "layer3_lte.h"
#include "layer2_lte_mac.h"

#ifdef LTE_LIB_LOG
#include "log_lte.h"

// Check if RB allocation consistency
#define CHECK_RB_ALLOCATION_CONSISTENCY 1

static std::string GetLteMacStateName(MacLteState state)
{
    if (state == MAC_LTE_POWER_OFF)
        return std::string("MAC_POWER_OFF");
    else if (state == MAC_LTE_IDEL)
        return std::string("MAC_IDLE");
    else if (state == MAC_LTE_RA_GRANT_WAITING)
        return std::string("MAC_RA_GRANT_WAITING");
    else if (state == MAC_LTE_RA_BACKOFF_WAITING)
        return std::string("MAC_RA_BACKOFF_WAITING");
    else if (state == MAC_LTE_DEFAULT_STATUS)
        return std::string("MAC_DEFAULT_STATUS");
    else {
        ERROR_Assert(false, "");
        return "";
    }
}

static std::string GetLteTimerName(int eventType)
{
    if (eventType == MSG_MAC_LTE_TtiTimerExpired)
        return std::string("t-TtiTimer");
    else if (eventType == MSG_MAC_LTE_RaBackoffWaitingTimerExpired)
        return std::string("t-RaBackoffWaiting");
    else if (eventType == MSG_MAC_LTE_RaGrantWaitingTimerExpired)
        return std::string("t-RaGrantWaiting");
    else {
        ERROR_Assert(false, "");
        return "";
    }
}
#endif // LTE_LIB_LOG

////////////////////////////////////////////////////////////////////////////
// Static function definition
////////////////////////////////////////////////////////////////////////////
// Initialize configurable parameters of LTE MAC Layer.
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface index
// \param nodeInput  Pointer to node input.
//
static void MacLteInitConfigurableParameters(
    Node* node, int interfaceIndex, const NodeInput* nodeInput);

// Multiplexing from MAC SDU to MAC PDU
//
// \param node           : IN  Pointer to node.
// \param interfaceIndex : IN  Interface index
// \param msg            : IN  MAC PDU
// \param sduList        : OUT : std:  List of MAC SDUs
//
static void MacLteDoDemultiplexing(
    Node* node, int interfaceIndex,
    Message* macPdu, std::list < Message* > &sduList);

// Add BSR
//
// \param node                 : IN  Pointer to node.
// \param interfaceIndex       : IN  Interface index
// \param msg                  : IN  MAC PDU
// \param rnti                 : IN  Destination's RNTI
//
static void MacLteAddBSR(
    Node* node, int interfaceIndex, Message* msg, const LteRnti& dstRnti);

// Print Statistics
//
// \param node                 : IN  Pointer to node.
// \param interfaceIndex       : IN  Interface index
// \param msg                  : IN  MAC PDU
//
// \return Transport Block Size
static void MacLtePrintStat(
    Node* node, int interfaceIndex, LteMacStatData* macStat);

static void MacLteLogOutputForRlcBuffer(Node* node, int interfaceIndex);
static void MacLteLogOutputPeriodic(Node* node, int interfaceIndex);

#ifdef LTE_LIB_LOG
#ifdef LTE_LIB_VALIDATION_LOG
void MacLteAddStatsForAllOfConnectedNodes(
        Node* node, UInt32 interfaceIndex,
        std::map < LteRnti, lte::LogLteAverager > &enabledStats,
        const LteRnti* key = NULL)
{
    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);

    std::vector < lte::ConnectionInfo > ciList;

    lte::LteLog::getConnectionInfoConnectingWithMe(
                node->nodeId,
                LteLayer2GetStationType(
                    node,
                    interfaceIndex) == LTE_STATION_TYPE_ENB,
                    ciList);

    if (!key)
    {
        for (int i = 0; i < ciList.size(); ++i){
            LteRnti oppositeRnti = LteRnti(ciList[i].nodeId,0);
            if (enabledStats.find(oppositeRnti)
                    == enabledStats.end())
            {
                enabledStats.insert(
                                std::pair < LteRnti, lte::LogLteAverager >
                                (oppositeRnti,lte::LogLteAverager()));
            }
        }
    }else
    {
        enabledStats.insert(std::pair < LteRnti, lte::LogLteAverager >
                                    (*key, lte::LogLteAverager()));
    }
}
#endif
#endif

////////////////////////////////////////////////////////////////////////////
// Function for MAC Layer
////////////////////////////////////////////////////////////////////////////
// Add Destination Info
//
// \param node               : IN  Pointer to node.
// \param interfaceIndex     : IN  Interface index
// \param msg                : IN  MAC PDU Message Structure
// \param oppositeRnti       : IN  Oposite RNTI
//
void MacLteAddDestinationInfo(Node* node,
                              Int32 interfaceIndex,
                              Message* msg,
                              const LteRnti& oppositeRnti)
{
    MacLteMsgDestinationInfo* info =
        (MacLteMsgDestinationInfo*)MESSAGE_AddInfo(
                                    node,
                                    msg,
                                    sizeof(MacLteMsgDestinationInfo),
                                    (UInt16)INFO_TYPE_LteMacDestinationInfo);
    ERROR_Assert(info != NULL, "");
    info->dstRnti = oppositeRnti;
#ifdef LTE_LIB_LOG
    lte::LteLog::DebugFormat(node, interfaceIndex,
        LTE_STRING_LAYER_TYPE_MAC,
        "DestInfo,RNTI=" LTE_STRING_FORMAT_RNTI,
        oppositeRnti.nodeId, oppositeRnti.interfaceIndex);
#endif
}

// Initialize LTE MAC Layer.
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface index
// \param nodeInput  Pointer to node input.
//
void MacLteInit(Node* node,
                UInt32 interfaceIndex,
                const NodeInput* nodeInput)
{
    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);

    MacLteInitConfigurableParameters(node, interfaceIndex, nodeInput);

    // random seed
    RANDOM_SetSeed(macData->seed,
                   node->globalSeed,
                   node->nodeId,
                   MAC_PROTOCOL_LTE,
                   interfaceIndex);

    // init state variable
    macData->ttiNumber = 0;
    macData->preambleTransmissionCounter = 1;
    macData->lastPropDelay = 0;

    // Init statistics
    macData->statData.numberOfSendingRaPreamble = 0;
    macData->statData.numberOfRecievingRaPreamble = 0;
    macData->statData.numberOfSendingRaGrant = 0;
    macData->statData.numberOfRecievingRaGrant = 0;
    macData->statData.numberOfEstablishment = 0;
    macData->statData.numberOfSduFromUpperLayer = 0;
    macData->statData.numberOfPduToLowerLayer = 0;
    macData->statData.numberOfPduFromLowerLayer = 0;
    macData->statData.numberOfSduToUpperLayer = 0;
    macData->statData.numberOfReceiveTb = 0;
    macData->statData.numberOfFirstReceiveTb = 0;
    macData->statData.numberOfSendTb = 0;
    macData->statData.numberOfFirstSendTb = 0;

    // Init State
    macData->macState = MAC_LTE_POWER_OFF;

    // Init Timer map
    macData->mapMacTimer.clear();

    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);

    if (stationType == LTE_STATION_TYPE_ENB)
    {
        MacLteSetNextTtiTimer(node, interfaceIndex);
    }

    if (stationType == LTE_STATION_TYPE_ENB)
    {
        macData->dlHarqTxEntity = new std::map< LteRnti, MacLteDlHarqTxEntity* >();
        macData->ulHarqRxEntity = new std::map< LteRnti, MacLteUlHarqRxEntity* >();
        macData->dlHarqRxEntity = NULL;
        macData->ulHarqTxEntity = NULL;
    }else
    {
        macData->dlHarqRxEntity = new MacLteDlHarqRxEntity(node, interfaceIndex);
        macData->ulHarqTxEntity = new MacLteUlHarqTxEntity(node, interfaceIndex);
        macData->dlHarqTxEntity = NULL;
        macData->ulHarqRxEntity = NULL;
    }

#ifdef LTE_LIB_LOG
#ifdef LTE_LIB_VALIDATION_LOG

    if (stationType == LTE_STATION_TYPE_ENB)
    {
        macData->avgEstimatedSinrDl =
                            new std::map < LteRnti, lte::LogLteAverager >;
        macData->avgEstimatedSinrUl =
                            new std::map < LteRnti, lte::LogLteAverager >;

        macData->avgNumAllocRbDl =
                            new std::map < LteRnti, lte::LogLteAverager >;
        macData->avgNumAllocRbUl =
                            new std::map < LteRnti, lte::LogLteAverager >;

        MacLteAddStatsForAllOfConnectedNodes(
                        node, interfaceIndex, *macData->avgEstimatedSinrDl);
        MacLteAddStatsForAllOfConnectedNodes(
                        node, interfaceIndex, *macData->avgEstimatedSinrUl);
        MacLteAddStatsForAllOfConnectedNodes(
                        node, interfaceIndex, *macData->avgNumAllocRbDl);
        MacLteAddStatsForAllOfConnectedNodes(
                        node, interfaceIndex, *macData->avgNumAllocRbUl);


    }else
    {
        macData->avgEstimatedSinrDl = NULL;
        macData->avgEstimatedSinrUl = NULL;
        macData->avgNumAllocRbDl    = NULL;
        macData->avgNumAllocRbUl    = NULL;
    }

#endif
#endif
#ifdef PARALLEL //Parallel
    PARALLEL_SetProtocolIsNotEOTCapable(node);
    PARALLEL_SetMinimumLookaheadForInterface(
            node,
            LTE_LAYER2_DEFAULT_DELAY_UNTIL_AIRBORN - 1*NANO_SECOND);
#endif //endParallel
}

// Print stats and clear protocol variables.
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface index
//
void MacLteFinalize(Node* node, UInt32 interfaceIndex)
{
    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);

    // PrintStat
    MacLtePrintStat(node, interfaceIndex, &(macData->statData));

#ifdef LTE_LIB_LOG
#ifdef LTE_LIB_VALIDATION_LOG
    std::map < LteRnti, lte::LogLteAverager > ::iterator it;


    // AvgEstimatedSinrDl

    if (macData->avgEstimatedSinrDl)
    {
        it = macData->avgEstimatedSinrDl->begin();
        for (; it != macData->avgEstimatedSinrDl->end(); ++it)
        {
            lte::LteLog::InfoFormat(node, interfaceIndex,
                    LTE_STRING_LAYER_TYPE_MAC,
                    "%s,%d,%e",
                    "AvgEstimatedSinrDl",
                    it->first.nodeId,
                    it->second.get());
        }

        delete macData->avgEstimatedSinrDl;
    }

    // AvgEstimatedSinrUl
    if (macData->avgEstimatedSinrUl)
    {
        it = macData->avgEstimatedSinrUl->begin();
        for (; it != macData->avgEstimatedSinrUl->end(); ++it)
        {
            lte::LteLog::InfoFormat(node, interfaceIndex,
                    LTE_STRING_LAYER_TYPE_MAC,
                    "%s,%d,%e",
                    "AvgEstimatedSinrUl",
                    it->first.nodeId,
                    it->second.get());
        }

        delete macData->avgEstimatedSinrUl;
    }

    // avgNumAllocRbDl
    if (macData->avgNumAllocRbDl)
    {
        it = macData->avgNumAllocRbDl->begin();
        for (; it != macData->avgNumAllocRbDl->end(); ++it)
        {
            lte::LteLog::InfoFormat(node, interfaceIndex,
                    LTE_STRING_LAYER_TYPE_MAC,
                    "%s,%d,%e",
                    "AvgNumAllocRbDl",
                    it->first.nodeId,
                    it->second.get());
        }

        delete macData->avgNumAllocRbDl;
    }

    // avgNumAllocRbUl
    if (macData->avgNumAllocRbUl)
    {
        it = macData->avgNumAllocRbUl->begin();
        for (; it != macData->avgNumAllocRbUl->end(); ++it)
        {
            lte::LteLog::InfoFormat(node, interfaceIndex,
                    LTE_STRING_LAYER_TYPE_MAC,
                    "%s,%d,%e",
                    "AvgNumAllocRbUl",
                    it->first.nodeId,
                    it->second.get());
        }

        delete macData->avgNumAllocRbUl;
    }
#endif
#endif
    if (macData->dlHarqTxEntity)
    {
        std::map<LteRnti, MacLteDlHarqTxEntity*>::iterator harqIte;
        harqIte = macData->dlHarqTxEntity->begin();
        while (harqIte != macData->dlHarqTxEntity->end())
        {
            delete harqIte->second;
            harqIte++;
        }

        delete macData->dlHarqTxEntity;
    }
    if (macData->ulHarqRxEntity)
    {
        std::map<LteRnti, MacLteUlHarqRxEntity*>::iterator harqIte;
        harqIte = macData->ulHarqRxEntity->begin();
        while (harqIte != macData->ulHarqRxEntity->end())
        {
            delete harqIte->second;
            harqIte++;
        }
        delete macData->ulHarqRxEntity;
    }

    if (macData->dlHarqRxEntity)
    {
        delete macData->dlHarqRxEntity;
    }
    if (macData->ulHarqTxEntity)
    {
        delete macData->ulHarqTxEntity;
    }
}

// Process Event.
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface index
// \param msg  Event message
//
void MacLteProcessEvent(Node* node, UInt32 interfaceIndex, Message* msg)
{
    LteMacData* macData =
        LteLayer2GetLteMacData(node, interfaceIndex);
    LteMapMessage* mapMacTimer = &(macData->mapMacTimer);


    int eventType = msg->eventType;
    LteMapMessage::iterator itr;
    mapMacTimer->erase(eventType);

#ifdef LTE_LIB_LOG
    std::string name = GetLteTimerName(eventType);
    lte::LteLog::Debug2Format(node, interfaceIndex,
        "MacLteProcessEvent",
        "%s is expired.", name.c_str());
#endif

    switch (eventType)
    {
        // MAC Event
        case MSG_MAC_LTE_TtiTimerExpired:
        {
            MacLteProcessTtiTimerExpired(
                node, interfaceIndex, msg);
            break;
        }
        case MSG_MAC_LTE_RaBackoffWaitingTimerExpired:
        {
            MacLteProcessRaBackoffWaitingTimerExpired(
                node, interfaceIndex, msg);
            break;
        }
        case MSG_MAC_LTE_RaGrantWaitingTimerExpired:
        {
            MacLteProcessRaGrantWaitingTimerExpired(
                node, interfaceIndex, msg);
            break;
        }
        default:
        {
            char errBuf[MAX_STRING_LENGTH] = {0};
            sprintf(errBuf,
                "Event Type(%d) is not supported "
                "by LTE MAC.", msg->eventType);
            ERROR_ReportError(errBuf);
            break;
        }
    }
    MESSAGE_Free(node, msg);
}

// Process TTI Timer expire event.
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface index
// \param msg  Event message
//
void MacLteProcessTtiTimerExpired(
    Node* node, UInt32 interfaceIndex, Message* msg)
{
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);

    switch (stationType) {
        case LTE_STATION_TYPE_ENB:
        {
            MacLteStartSchedulingAtENB(
                node, interfaceIndex, msg);
            break;
        }
        case LTE_STATION_TYPE_UE:
        {
            MacLteStartSchedulingAtUE(
                node, interfaceIndex, msg);
            break;
        }
        default:
        {
            char errBuf[MAX_STRING_LENGTH] = {0};
            sprintf(errBuf,
                "LTE do not support LteStationType(%d)\n"
                "Please check LteStationType.\n"
                , stationType);
            ERROR_ReportError(errBuf);
            break;
        }
    }
}

// Start point for eNB scheduling.
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface index
// \param msg  Event message
//
void MacLteStartSchedulingAtENB(
    Node* node, UInt32 interfaceIndex, Message* msg)
{
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);
    ERROR_Assert(stationType == LTE_STATION_TYPE_ENB,
        "This function should be called from only eNB.");
    int phyIndex =
        LteGetPhyIndexFromMacInterfaceIndex(node, interfaceIndex);

    Message* listMsg = NULL; // send to PHY
    Message* curMsg = NULL;
    UInt32 numDlResourceBlocks = 0;; // Allocated DL RBs
    UInt32 sumTbs = 0;

    std::map<LteRnti, MacLteDlHarqTxEntity*>::iterator harqDlTxIte;
    std::map<LteRnti, MacLteUlHarqRxEntity*>::iterator harqUlRxIte;

    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);
    macData->ttiNumber++; // only eNB

    if (macData->macState != MAC_LTE_POWER_OFF)
    {
        // for log
        MacLteLogOutputPeriodic(node, interfaceIndex);
        MacLteLogOutputForRlcBuffer(node, interfaceIndex);

    
        // get HARQ feedback message
        for (harqUlRxIte = macData->ulHarqRxEntity->begin(); 
            harqUlRxIte != macData->ulHarqRxEntity->end(); harqUlRxIte++)
        {
            Message* feedbackMsg = NULL;
            harqUlRxIte->second->GetFeedbackMessage(node, phyIndex, &feedbackMsg, &harqUlRxIte->first);
            // Add send message list
            if (feedbackMsg != NULL)
            {
                if (listMsg == NULL)
                {
                    listMsg = feedbackMsg;
                    curMsg = feedbackMsg;
                }
                else
                {
                    curMsg->next = feedbackMsg;
                    curMsg = feedbackMsg;
                }
            }
        }

        // DL scheduling start
        std::vector < LteDlSchedulingResultInfo > dlSchedulingResults;
        LteScheduler* scheduler =
                            LteLayer2GetScheduler(node, interfaceIndex);

        ERROR_Assert(scheduler != NULL, "Scheduler is not instanced.");

        scheduler->prepareForScheduleTti(
            MacLteGetTtiNumber(node, interfaceIndex));
        scheduler->scheduleDlTti(dlSchedulingResults);

#ifdef LTE_LIB_LOG
        dynamic_cast<LteSchedulerENB*>(scheduler)->
                debugOutputSchedulingResultLog(dlSchedulingResults);
#endif

        UInt32 actualSendingByte = 0;

        for (size_t dlUe = 0; dlUe < dlSchedulingResults.size(); dlUe++)
        {
            LteDlSchedulingResultInfo* dlSchedulingResult =
                &(dlSchedulingResults[dlUe]);

            LteRnti oppositeRnti = dlSchedulingResult->rnti;
            int numTB = dlSchedulingResult->numTransportBlock;
            int numRB = MacLteGetNumberOfRbForDl(dlSchedulingResult);

            // HARQ start signal transimission notification
            harqDlTxIte = macData->dlHarqTxEntity->find(dlSchedulingResult->rnti);

            if (harqDlTxIte != macData->dlHarqTxEntity->end() )
            {
                // add retry or new data to msglist
                harqDlTxIte->second->OnStartSignalTransmission(
                        dlSchedulingResult, &listMsg, &curMsg);
                numDlResourceBlocks += numRB;

                continue;
            }
        }

#ifdef LTE_LIB_LOG
        lte::LteLog::Debug2Format(node, interfaceIndex,
            LTE_STRING_LAYER_TYPE_MAC,
            "ActualSendingByte,%d", actualSendingByte);
#endif
        // DL scheduling end

        // check uplink scheduling message (no message from UE)
        for (harqUlRxIte = macData->ulHarqRxEntity->begin();
            harqUlRxIte != macData->ulHarqRxEntity->end(); harqUlRxIte++)
        {
            harqUlRxIte->second->OnBeforeStartSignalTransmission();
        }


        // UL sceduling start
        std::vector < LteUlSchedulingResultInfo > ulSchedulingResults;

        ERROR_Assert(scheduler != NULL, "Scheduler is not instanced.");

        scheduler->scheduleUlTti(ulSchedulingResults);
#ifdef LTE_LIB_LOG
        dynamic_cast<LteSchedulerENB*>(scheduler)->
                debugOutputSchedulingResultLog(ulSchedulingResults);
#endif

        for (size_t ulUe = 0; ulUe < ulSchedulingResults.size(); ulUe++)
        {
            LteUlSchedulingResultInfo* result =
                &(ulSchedulingResults[ulUe]);
            LteRnti oppositeRnti = result->rnti;
            Message* ulDciMsg = MESSAGE_Alloc(node, 0, 0, 0);

            
            // HARQ start signal transimission notification
            harqUlRxIte = macData->ulHarqRxEntity->find(result->rnti);

            if (harqUlRxIte != macData->ulHarqRxEntity->end())
            {
                // add dci0format
                harqUlRxIte->second->OnStartSignalTransmission(
                        result, ulDciMsg);
            }

#ifdef LTE_LIB_LOG
#ifdef LTE_LIB_VALIDATION_LOG
            if (node->getNodeTime()*SECOND >=
                lte::LteLog::getValidationStatOffset())
            {
                (*macData->avgNumAllocRbUl)[oppositeRnti].add(
                        (double)result->numResourceBlocks);
            }
#endif
#endif

#ifdef ADDON_DB
            LteMacAddStatsDBInfo(node, ulDciMsg);
            LteMacUpdateStatsDBInfo(node,
                                    ulDciMsg,
                                    0,
                                    sizeof(LteDciFormat0),
                                    TRUE);
            LteMacStatsDBCheckForMsgSequenceNum(node, ulDciMsg);
#endif

            // Add Destination Info
            MacLteAddDestinationInfo(
                node, interfaceIndex, ulDciMsg, oppositeRnti);

#ifdef ADDON_DB
            // These msgs will never reach to receiver side LTE Layer2 MAC.
            // The infomation just get stored on receiver LTE PHY.
            // So, recording for stats DB at receiver side whereever they
            // are actually retrieved and saved.
            HandleMacDBEvents(
                node,
                ulDciMsg,
                node->macData[interfaceIndex]->phyNumber,
                interfaceIndex,
                MAC_SendToPhy,
                node->macData[interfaceIndex]->macProtocol);

            HandleMacDBConnectivity(node,
                                    interfaceIndex,
                                    ulDciMsg,
                                    MAC_SendToPhy);

            if (node->macData[interfaceIndex]->macStats)
            {
                LteStatsDbSduPduInfo* ulDciMsgInfo = (LteStatsDbSduPduInfo*)
                        MESSAGE_ReturnInfo(
                                 ulDciMsg,
                                (UInt16)INFO_TYPE_LteStatsDbSduPduInfo);
                node->macData[interfaceIndex]->stats->AddFrameSentDataPoints(
                                                      node,
                                                      ulDciMsg,
                                                      STAT_Unicast,
                                                      ulDciMsgInfo->ctrlSize,
                                                      ulDciMsgInfo->dataSize,
                                                      interfaceIndex,
                                                      oppositeRnti.nodeId);
            }
#endif
            // Add send message list
            if (listMsg == NULL)
            {
                listMsg = ulDciMsg;
                curMsg = listMsg;
            }
            else
            {
                curMsg->next = ulDciMsg;
                curMsg = ulDciMsg;
            }
        }

        // UL scheduling end

#ifdef LTE_LIB_LOG
#if CHECK_RB_ALLOCATION_CONSISTENCY

        // Check if RB allocation is not overlapped
        // over all of the scheduling results

        Message* txMsg = listMsg;

        std::vector < bool > dlRbAllocation(PHY_LTE_MAX_NUM_RB, false);
        std::vector < bool > ulRbAllocation(PHY_LTE_MAX_NUM_RB, false);

        while (txMsg != NULL)
        {
            // DCI format-0?
            LteDciFormat0* Dci0Info =
                (LteDciFormat0*)MESSAGE_ReturnInfo(msg,
                        (unsigned short)INFO_TYPE_LteDci0Info);

            // DCI format-1?
            LteDciFormat1* Dci1Info =
                (LteDciFormat1*)MESSAGE_ReturnInfo(msg,
                        (unsigned short)INFO_TYPE_LteDci1Info);

            // DCI format-2a?
            LteDciFormat2a* Dci2aInfo =
                (LteDciFormat2a*)MESSAGE_ReturnInfo(msg,
                        (unsigned short)INFO_TYPE_LteDci2aInfo);

            if (Dci0Info)
            {
                for (int b = Dci0Info->usedRB_start;
                    b < Dci0Info->usedRB_length;
                    ++b)
                {
                    ERROR_Assert(ulRbAllocation[b] == false,
                            "Resource block allocation is overlapping.");

                    ulRbAllocation[b] = true;
                }
            }else if (Dci1Info)
            {
                for (int b = 0; b < PHY_LTE_MAX_NUM_RB; ++b)
                {
                    if (Dci1Info->usedRB_list[b] == 1)
                    {
                        ERROR_Assert(dlRbAllocation[b] == false,
                                "Resource block allocation is overlapping.");
                        dlRbAllocation[b] = true;
                    }
                }
            }else if (Dci2aInfo)
            {
                for (int b = 0; b < PHY_LTE_MAX_NUM_RB; ++b)
                {
                    if (Dci2aInfo->usedRB_list[b] == 1)
                    {
                        ERROR_Assert(dlRbAllocation[b] == false,
                                "Resource block allocation is overlapping.");
                        dlRbAllocation[b] = true;
                    }
                }
            }

            txMsg = txMsg->next;
        }

#endif // CHECK_RB_ALLOCATION_CONSISTENCY
#endif // LTE_LIB_LOG

        // Pack message
        Message* sendMsg = NULL;
        if (listMsg == NULL)
        {
            sendMsg = MESSAGE_Alloc(node, 0, 0, 0);
            MESSAGE_AddInfo(
                node, sendMsg, 1, INFO_TYPE_LteMacNoTransportBlock);

#ifdef ADDON_DB
            LteMacAddStatsDBInfo(node, sendMsg);
            LteMacUpdateStatsDBInfo(node, sendMsg, 0, 1, TRUE);

            LteMacStatsDBCheckForMsgSequenceNum(node, sendMsg);

            // currently deferred of calling HandleMacDBEvents() here
#endif
        }
        else
        {
            sendMsg = MESSAGE_PackMessage(
                                node, listMsg, TRACE_MAC_LTE, NULL);
            if (ulSchedulingResults.size() > 0)
            {
                MacLteTxInfo* txInfo =
                    (MacLteTxInfo*)MESSAGE_AddInfo(
                        node, sendMsg,
                        sizeof(MacLteTxInfo), INFO_TYPE_LteMacTxInfo);
                txInfo->numResourceBlocks = numDlResourceBlocks;
            }
#ifdef ADDON_DB
            LteMacStatsDBCheckForMsgSequenceNum(node, sendMsg);
#endif
        }

        if (node->guiOption)
        {
            GUI_Broadcast(
                node->nodeId,
                GUI_MAC_LAYER,
                GUI_DEFAULT_DATA_TYPE,
                interfaceIndex,
                node->getNodeTime());
        }

        // Send to PHY Layer
        PHY_StartTransmittingSignal(
            node, phyIndex, sendMsg,
            LTE_LAYER2_DEFAULT_USE_SPECIFIED_DELAY,
            LTE_LAYER2_DEFAULT_DELAY_UNTIL_AIRBORN);
    }

    // Set Next Timer
    MacLteSetNextTtiTimer(node, interfaceIndex);
}

// Start point for UE scheduling.
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface index
// \param msg  Event message
//
void MacLteStartSchedulingAtUE(
    Node* node, UInt32 interfaceIndex, Message* msg)
{
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);
    ERROR_Assert(stationType == LTE_STATION_TYPE_UE,
        "This function should be called from only UE.");
    int phyIndex =
        LteGetPhyIndexFromMacInterfaceIndex(node, interfaceIndex);
    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);

    Message* listMsg = NULL; // send to PHY
    Message* nextMsg = NULL;
    UInt32 numUlResourceBlocks = 0;

    // for log
    MacLteLogOutputForRlcBuffer(node, interfaceIndex);
    MacLteLogOutputPeriodic(node, interfaceIndex);

    // UL scheduling results
    std::vector < LteUlSchedulingResultInfo > ulSchedulingResult;
    LteScheduler* scheduler = LteLayer2GetScheduler(node, interfaceIndex);

    ERROR_Assert(scheduler != NULL, "Scheduler is not instanced.");

    // Indicates the TTI number of scheduling target signal.
    scheduler->prepareForScheduleTti(
        (MacLteGetTtiNumber(node, interfaceIndex) + 1 ));
    scheduler->scheduleUlTti(ulSchedulingResult);

    Message* feedbackMsg = NULL;
    int msgCnt = 0;
    if (macData->ulHarqTxEntity->OnSignalTransmission(
                node, phyIndex, &ulSchedulingResult, &listMsg, &nextMsg, &numUlResourceBlocks) == TRUE)
    {
        msgCnt++;
    }

    // Pack message
    Message* sendMsg = NULL;
    if (listMsg == NULL)
    {
        sendMsg = MESSAGE_Alloc(node, 0, 0, 0);
        MESSAGE_AddInfo(node, sendMsg, 1, INFO_TYPE_LteMacNoTransportBlock);

#ifdef ADDON_DB
        LteMacAddStatsDBInfo(node, sendMsg);
        LteMacUpdateStatsDBInfo(node, sendMsg, 0, 1, TRUE);

        LteMacStatsDBCheckForMsgSequenceNum(node, sendMsg);

        // currently deferred of calling HandleMacDBEvents() here
#endif
    }
    else
    {
        sendMsg = MESSAGE_PackMessage(node, listMsg, TRACE_MAC_LTE, NULL);
        //if (ulSchedulingResult.size() > 0)
        if (msgCnt > 0)
        {
            MacLteTxInfo* txInfo =
                (MacLteTxInfo*)MESSAGE_AddInfo(
                    node, sendMsg,
                    sizeof(MacLteTxInfo), INFO_TYPE_LteMacTxInfo);
            txInfo->numResourceBlocks = numUlResourceBlocks;
        }
#ifdef ADDON_DB
        LteMacStatsDBCheckForMsgSequenceNum(node, sendMsg);
#endif
    }

    ListLteRnti connectedList;
    Layer3LteGetConnectionList(node, interfaceIndex, &connectedList,
        LAYER3_LTE_CONNECTION_CONNECTED);
    if (connectedList.size() > 0)
    {
        const LteRnti& enbRnti =
            Layer3LteGetConnectedEnb(node, interfaceIndex);
        // Add BSR
        MacLteAddBSR(node, interfaceIndex, sendMsg, enbRnti);

        // Add HARQ DL feedback message
        macData->dlHarqRxEntity->GetHarqFeedbackMessage(node, phyIndex, sendMsg, &enbRnti);
    }

    // Send to PHY Layer
    PHY_StartTransmittingSignal(
        node, phyIndex, sendMsg,
        LTE_LAYER2_DEFAULT_USE_SPECIFIED_DELAY,
        LTE_LAYER2_DEFAULT_DELAY_UNTIL_AIRBORN);

    // Set Next Timer
    MacLteSetNextTtiTimer(node, interfaceIndex);
}

// Initialize configurable parameters of LTE MAC Layer.
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface index
// \param nodeInput  Pointer to node input.
//
static void MacLteInitConfigurableParameters(
    Node* node, int interfaceIndex, const NodeInput* nodeInput)
{
    BOOL wasFound = false;
    char errBuf[MAX_STRING_LENGTH] = {0};
    char warnBuf[MAX_STRING_LENGTH] = {0};
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);

    clocktype retTime = 0;
    double retDouble = 0.0;
    int retInt = 0;

    LteMacConfig* macConfig = GetLteMacConfig(node, interfaceIndex);
    int phyIndex =
        LteGetPhyIndexFromMacInterfaceIndex(node, interfaceIndex);

    // eNB settings
    if (stationType == LTE_STATION_TYPE_ENB)
    {
        // LteRrcConfig settings
        // MAC_LTE_STRING_RA_BACKOFF_TIME
        IO_ReadTime(node,
                    node->nodeId,
                    interfaceIndex,
                    nodeInput,
                    MAC_LTE_STRING_RA_BACKOFF_TIME,
                    &wasFound,
                    &retTime);
        if (wasFound == TRUE)
        {
            if (retTime >= 0 && retTime <= CLOCKTYPE_MAX)
            {
                macConfig->raBackoffTime = retTime;
            }
            else
            {
                sprintf(errBuf,
                    "MAC-LTE: ra-BackoffTime should not be negative integer."
                    " Please check %s.", MAC_LTE_STRING_RA_BACKOFF_TIME);
                ERROR_ReportError(errBuf);
            }
        }
        else
        {
            sprintf(warnBuf,
                    "MAC-LTE: RA backoff time should be set. "
                    "Change RA backoff time to %" TYPES_64BITFMT "dmsec.",
                    MAC_LTE_DEFAULT_RA_BACKOFF_TIME/MILLI_SECOND);
            ERROR_ReportWarning(warnBuf);
            macConfig->raBackoffTime = MAC_LTE_DEFAULT_RA_BACKOFF_TIME;
        }

        // MAC_LTE_STRING_RA_PREAMBLE_INITIAL_RECEIVED_TARGET_POWER
        IO_ReadDouble(
            node,
            node->nodeId,
            interfaceIndex,
            nodeInput,
            MAC_LTE_STRING_RA_PREAMBLE_INITIAL_RECEIVED_TARGET_POWER,
            &wasFound,
            &retDouble);
        if (wasFound == TRUE)
        {
            if (retDouble >= LTE_NEGATIVE_INFINITY_POWER_dBm &&
               retDouble <= LTE_INFINITY_POWER_dBm)
            {
                macConfig->raPreambleInitialReceivedTargetPower = retDouble;
            }
            else
            {
                sprintf(errBuf,
                    "MAC-LTE: preambleInitialReceivedTargetPower "
                    "should be %.2lf to %.2lf. "
                    "Please check %s",
                    (double)LTE_NEGATIVE_INFINITY_POWER_dBm,
                    (double)LTE_INFINITY_POWER_dBm,
                    MAC_LTE_STRING_RA_PREAMBLE_INITIAL_RECEIVED_TARGET_POWER);
                ERROR_ReportError(errBuf);
            }
        }
        else
        {
            sprintf(
                warnBuf,
                "MAC-LTE: preambleInitialReceivedTargetPower "
                "should be set. "
                "Change preambleInitialReceivedTargetPower to %f dBm.",
                MAC_LTE_DEFAULT_RA_PREAMBLE_INITIAL_RECEIVED_TARGET_POWER);
            ERROR_ReportWarning(warnBuf);
            macConfig->raPreambleInitialReceivedTargetPower =
                MAC_LTE_DEFAULT_RA_PREAMBLE_INITIAL_RECEIVED_TARGET_POWER;
        }

        // MAC-LTE-RA-POWER-RAMPING-STEP
        IO_ReadDouble(node,
                      node->nodeId,
                      interfaceIndex,
                      nodeInput,
                      MAC_LTE_STRING_RA_POWER_RAMPING_STEP,
                      &wasFound,
                      &retDouble);
        if (wasFound == TRUE)
        {
            if (retDouble >= 0 &&
               retDouble <= LTE_INFINITY_POWER_dBm)
            {
                macConfig->raPowerRampingStep = retDouble;
            }
            else
            {
                sprintf(errBuf,
                    "PHY-LTE: RA powerRampingStep "
                    "should be %.2lf to %.2lf. "
                    "Please check %s",
                    0.0,
                    (double)LTE_INFINITY_POWER_dBm,
                    MAC_LTE_STRING_RA_POWER_RAMPING_STEP);
                ERROR_ReportError(errBuf);
            }
        }
        else
        {
            sprintf(warnBuf,
                    "MAC-LTE: RA powerRampingStep should be set. "
                    "Change RA powerRampingStep to %2.1f dB.",
                    MAC_LTE_DEFAULT_RA_POWER_RAMPING_STEP);
            ERROR_ReportWarning(warnBuf);
            macConfig->raPowerRampingStep =
                MAC_LTE_DEFAULT_RA_POWER_RAMPING_STEP;
        }

        // MAC-LTE-RA-PREAMBLE-TRANS-MAX
        IO_ReadInt(node,
                   node->nodeId,
                   interfaceIndex,
                   nodeInput,
                   MAC_LTE_STRING_RA_PREAMBLE_TRANS_MAX,
                   &wasFound,
                   &retInt);
        if (wasFound == TRUE)
        {
            if (retInt >= 0 && retInt <= INT_MAX)
            {
                macConfig->raPreambleTransMax = retInt;
            }
            else
            {
                sprintf(errBuf,
                    "MAC-LTE: RA preambleTransMax "
                    "should be %d to %d. "
                    "Please check %s",
                    0, INT_MAX,
                    MAC_LTE_STRING_RA_PREAMBLE_TRANS_MAX);
                ERROR_ReportError(errBuf);
            }
        }
        else
        {
            sprintf(warnBuf,
                    "MAC-LTE: RA preambleTransMax should be set. "
                    "Change RA preambleTransMax to %d. ",
                    MAC_LTE_DEFAULT_RA_PREAMBLE_TRANS_MAX);
            ERROR_ReportWarning(warnBuf);
            macConfig->raPreambleTransMax =
                MAC_LTE_DEFAULT_RA_PREAMBLE_TRANS_MAX;
        }

        // MAC-LTE-RA-RESPONSE-WINDOW-SIZE
        IO_ReadInt(node,
                   node->nodeId,
                   interfaceIndex,
                   nodeInput,
                   MAC_LTE_STRING_RA_RESPONSE_WINDOW_SIZE,
                   &wasFound,
                   &retInt);
        if (wasFound == TRUE)
        {
            if (retInt >= 3 && retInt <= INT_MAX)
            {
                macConfig->raResponseWindowSize = retInt;
            }
            else
            {
                sprintf(errBuf,
                    "MAC-LTE: RA Response window size "
                    "should be %d to %d. "
                    "Please check %s.",
                    3, INT_MAX,
                    MAC_LTE_STRING_RA_RESPONSE_WINDOW_SIZE);
                ERROR_ReportError(errBuf);
            }
        }
        else
        {
            sprintf(warnBuf,
                    "MAC-LTE: RA Response window size should be set. "
                    "Change RA Response window size to %d.",
                    MAC_LTE_DEFAULT_RA_RESPONSE_WINDOW_SIZE);
            ERROR_ReportWarning(warnBuf);
            macConfig->raResponseWindowSize =
                MAC_LTE_DEFAULT_RA_RESPONSE_WINDOW_SIZE;
        }

        // MAC-LTE-RA-PRACH-CONFIG-INDEX
        IO_ReadInt(node,
                   node->nodeId,
                   interfaceIndex,
                   nodeInput,
                   MAC_LTE_STRING_RA_PRACH_CONFIG_INDEX,
                   &wasFound,
                   &retInt);
        if (wasFound == TRUE)
        {
            if (retInt >= 3 && retInt <= 14)
            {
                macConfig->raPrachConfigIndex = retInt;
            }
            else
            {
                sprintf(errBuf,
                    "MAC-LTE: prach-ConfigIndex "
                    "should be %d to %d. "
                    "Please check %s.",
                    3, 14,
                    MAC_LTE_STRING_RA_PRACH_CONFIG_INDEX);
                ERROR_ReportError(errBuf);
            }
        }
        else
        {
            sprintf(warnBuf,
                    "MAC-LTE: prach-ConfigIndex should be set. "
                    "Change prach-ConfigIndex to %d.",
                    MAC_LTE_DEFAULT_RA_PRACH_CONFIG_INDEX);
            ERROR_ReportWarning(warnBuf);
            macConfig->raPrachConfigIndex =
                MAC_LTE_DEFAULT_RA_PRACH_CONFIG_INDEX;
        }

        // MAC-LTE-PERIODIC-BSR-TTI
        IO_ReadInt(node,
                   node->nodeId,
                   interfaceIndex,
                   nodeInput,
                   MAC_LTE_STRING_PERIODIC_BSR_TTI,
                   &wasFound,
                   &retInt);
        if (wasFound == TRUE)
        {
            if (retInt >= 1 && retInt <= INT_MAX)
            {
                macConfig->periodicBSRTimer = retInt;
            }
            else
            {
                sprintf(errBuf,
                    "MAC-LTE: Periodic BSR Timer "
                    "should be %d to %d. "
                    "Please check %s.",
                    1, INT_MAX,
                    MAC_LTE_STRING_PERIODIC_BSR_TTI);
                ERROR_ReportError(errBuf);
            }
        }
        else
        {
            sprintf(warnBuf,
                    "MAC-LTE: Periodic BSR Timer should be set. "
                    "Change Periodic BSR Timer to %d.",
                    MAC_LTE_DEFAULT_PERIODIC_BSR_TTI);
            ERROR_ReportWarning(warnBuf);
            macConfig->periodicBSRTimer =
                MAC_LTE_DEFAULT_PERIODIC_BSR_TTI;
        }

        // MAC-LTE-TRANSMISSION-MODE
        IO_ReadInt(node,
            node->nodeId,
            interfaceIndex,
            nodeInput,
            MAC_LTE_STRING_TRANSMISSION_MODE,
            &wasFound,
            &retInt);

        if (wasFound == TRUE)
        {
            if (retInt < 1 || retInt > 3)
            {
                sprintf(errBuf,
                    "MAC-LTE: Transmission mode %d is not supported. ",
                    retInt);

                ERROR_ReportError(errBuf);
            }

            macConfig->transmissionMode = retInt;
        }
        else
        {
            sprintf(warnBuf,
                    "MAC-LTE: Transmission mode should be set. "
                    "Change Transmission mode to %d.",
                    MAC_LTE_DEFAULT_TRANSMISSION_MODE);
            ERROR_ReportWarning(warnBuf);
            macConfig->transmissionMode = MAC_LTE_DEFAULT_TRANSMISSION_MODE;
        }

        // Check antenna configuration consistency
        switch (macConfig->transmissionMode)
        {
        case 1:
            if (PhyLteGetNumTxAntennas(node, phyIndex) != 1)
            {
                sprintf(errBuf,
                    "MAC-LTE: Number of tx antennas at eNB has to be 1 for "
                    "transmission mode 1. "
                    "Please check tx antenna configuration.");

                ERROR_ReportError(errBuf);
            }
            break;
        case 2:
            if (PhyLteGetNumTxAntennas(node, phyIndex) != 2)
            {
                sprintf(errBuf,
                    "MAC-LTE: Number of tx antennas at eNB has to be 2 for "
                    "transmission mode 2. "
                    "Please check tx antenna configuration.");

                ERROR_ReportError(errBuf);
            }
            break;
        case 3:
            if (PhyLteGetNumTxAntennas(node, phyIndex) != 2)
            {
                sprintf(errBuf,
                    "MAC-LTE: Number of tx antennas at eNB has to be 2 for "
                    "transmission mode 3. "
                    "Please check tx antenna configuration.");

                ERROR_ReportError(errBuf);
            }
            break;
        default:
            ERROR_Assert(FALSE, "Invalid transmission mode");
        }
        
        // MAC-LTE-MAX-HARQ-TX
        IO_ReadInt(node,
                   node->nodeId,
                   interfaceIndex,
                   nodeInput,
                   MAC_LTE_STRING_MAX_HARQ_TX,
                   &wasFound,
                   &retInt);
        if (wasFound == TRUE)
        {
            if (retInt >= 1 && retInt <= 4)
            {
                macConfig->maxHarqTx = retInt;
            }
            else
            {
                sprintf(errBuf,
                    "MAC-LTE: Max HARQ Tx "
                    "should be %d to %d. "
                    "Please check %s",
                    1, 4,
                    MAC_LTE_STRING_MAX_HARQ_TX);
                ERROR_ReportError(errBuf);
            }
        }
        else
        {
            sprintf(warnBuf,
                    "MAC-LTE: Max HARQ Tx should be set. "
                    "Change Max HARQ Tx to %d. ",
                    MAC_LTE_DEFAULT_MAX_HARQ_TX);
            ERROR_ReportWarning(warnBuf);
            macConfig->maxHarqTx =
                MAC_LTE_DEFAULT_MAX_HARQ_TX;
        }
    }
}

// Multiplexing from MAC SDU to MAC PDU
//
// \param node           : IN  Pointer to node.
// \param interfaceIndex : IN  Interface index
// \param tbSize         : IN  Transport Block Size
// \param sduList        : IN  : std:  List of MAC SDUs
// \param msg            : OUT  MAC PDU
//
void MacLteDoMultiplexing(
    Node* node, int interfaceIndex, const LteRnti& oppositeRnti,
    int bearerId, int tbSize,
    std::list < Message* > &sduList, Message** macPdu,
    UInt32* withoutPaddingByte)
{
    Message* msgList = NULL;
#ifdef ADDON_DB
    UInt32 dataSize = 0;
    UInt32 ctrlSize = 0;
#endif

    if (sduList.size() > 0)
    {
        // for each SDUs
        std::list < Message* > ::iterator itr;
        for (itr  = sduList.begin();
            itr != sduList.end();
            ++itr)
        {
            Message* curSdu = *itr;
            UInt16 curSduSize = 0;
            curSduSize = (UInt16)MESSAGE_ReturnPacketSize(curSdu);

            if (msgList == NULL)
            {
                msgList = curSdu;
            }

            // Add MAC Subheader to MAC SDU
            LteMacRrelcidflWith15bitSubHeader* subHeader =
                (LteMacRrelcidflWith15bitSubHeader*)MESSAGE_AddInfo(
                    node,
                    curSdu,
                    sizeof(LteMacRrelcidflWith15bitSubHeader),
                    INFO_TYPE_LteMacRRELCIDFLWith15bitSubHeader);
            subHeader->e = MAC_LTE_DEFAULT_E_FIELD;
            subHeader->lcid = MAC_LTE_DEFAULT_LCID_FIELD;
            subHeader->f = MAC_LTE_DEFAULT_F_FIELD;
            subHeader->l = curSduSize;

            // add virtualPayloadSize to MAC PDU (MAC Subheader + MAC SDU)
            MESSAGE_AddVirtualPayload(
                node, curSdu, MAC_LTE_RRELCIDFL_WITH_15BIT_SUBHEADER_SIZE);
#ifdef ADDON_DB
            LteStatsDbSduPduInfo* info = (LteStatsDbSduPduInfo*)
                                MESSAGE_ReturnInfo(
                                     curSdu,
                                    (UInt16)INFO_TYPE_LteStatsDbSduPduInfo);
            info->ctrlSize += MAC_LTE_RRELCIDFL_WITH_15BIT_SUBHEADER_SIZE;
            dataSize += info->dataSize;
            ctrlSize += info->ctrlSize;
#endif
            std::list<Message*>::iterator tmpItr = itr;
            if (++tmpItr != sduList.end())
            {
                curSdu->next = *tmpItr;
            }
            else
            {
                curSdu->next = NULL;
            }
            *withoutPaddingByte +=
                curSduSize + MAC_LTE_RRELCIDFL_WITH_15BIT_SUBHEADER_SIZE;
        }

        // serialize & padding
        std::string buf;
        int numMsg = MESSAGE_SerializeMsgList(node, msgList, buf);
        msgList = NULL;
        *macPdu = MESSAGE_Alloc(node, 0, 0, 0);
        MESSAGE_AddVirtualPayload(node, (*macPdu), tbSize);
        char* multiplexingInfo =
            MESSAGE_AddInfo(node, *macPdu,
                (int)buf.size(), INFO_TYPE_LteMacMultiplexingMsg);
        memcpy(multiplexingInfo, buf.data(), buf.size());

        // number of sdu
        int* numSdu = (int*)MESSAGE_AddInfo(
                                node,
                                (*macPdu),
                                sizeof(int),
                                INFO_TYPE_LteMacNumMultiplexingMsg);
        *numSdu = numMsg;
#ifdef ADDON_DB
        // adding stats-db info to new macPdu mesage
        LteMacAddStatsDBInfo(node, (*macPdu));
        LteMacUpdateStatsDBInfo(node,
                                (*macPdu),
                                dataSize,
                                ctrlSize,
                                FALSE);
#endif
    }
}

// Multiplexing from MAC SDU to MAC PDU
//
// \param node           : IN  Pointer to node.
// \param interfaceIndex : IN  Interface index
// \param msg            : IN  MAC PDU
// \param sduList        : OUT : std:  List of MAC SDUs
//
static void MacLteDoDemultiplexing(
    Node* node, int interfaceIndex,
    Message* macPdu, std::list < Message* > &sduList)
{
    sduList.clear();
    char* retInfo = NULL;

    // num msg
    retInfo =
        MESSAGE_ReturnInfo(macPdu, INFO_TYPE_LteMacNumMultiplexingMsg);
    ERROR_Assert(retInfo != NULL,
        "INFO_TYPE_LteMacNumMultiplexingMsg is not found.");
    int numMsg = 0;
    memcpy(&numMsg, retInfo, sizeof(int));

    // serialized msg
    retInfo = MESSAGE_ReturnInfo(macPdu, INFO_TYPE_LteMacMultiplexingMsg);
    ERROR_Assert(retInfo != NULL,
        "INFO_TYPE_LteMacMultiplexingMsg is not found.");

    // unserialize
    int bufIndex = 0;
    Message* msgList =
        MESSAGE_UnserializeMsgList(node->partitionData, retInfo, bufIndex, numMsg);

    Message* curMsg = msgList;
    while (curMsg != NULL)
    {
        MESSAGE_RemoveInfo(node, curMsg,
            INFO_TYPE_LteMacRRELCIDFLWith15bitSubHeader);
        MESSAGE_RemoveVirtualPayload(node, curMsg,
            MAC_LTE_RRELCIDFL_WITH_15BIT_SUBHEADER_SIZE);
#ifdef ADDON_DB
        // before push back, decrease the header size in stats-db ctrl info
        LteStatsDbSduPduInfo* info = (LteStatsDbSduPduInfo*)
                            MESSAGE_ReturnInfo(
                                 curMsg,
                                (UInt16)INFO_TYPE_LteStatsDbSduPduInfo);
        info->ctrlSize = info->ctrlSize
                            - MAC_LTE_RRELCIDFL_WITH_15BIT_SUBHEADER_SIZE;
#endif
        sduList.push_back(curMsg);
        curMsg = curMsg->next;
    }
    MESSAGE_Free(node, macPdu);
}

// Add BSR
//
// \param node                 : IN  Pointer to node.
// \param interfaceIndex       : IN  Interface index
// \param msg                  : IN  MAC PDU
// \param rnti                 : IN  Destination's RNTI
//
static void MacLteAddBSR(
    Node* node, int interfaceIndex, Message* msg, const LteRnti& dstRnti)
{
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);
    ERROR_Assert(stationType == LTE_STATION_TYPE_UE,
        "This function should be called from only UE.");

    LteMacConfig* macConfig = GetLteMacConfig(node, interfaceIndex);

    UInt64 ttiNumber = MacLteGetTtiNumber(node, interfaceIndex);
    if (ttiNumber % macConfig->periodicBSRTimer == 0)
    {
        UInt32 bufferSize =
            LteRlcSendableByteInQueue(
                node, interfaceIndex, dstRnti, LTE_DEFAULT_BEARER_ID);

        int bsrSize = sizeof(LteBsrInfo);
        LteBsrInfo* bsrInfo = (LteBsrInfo*)MESSAGE_AddInfo(
            node, msg, bsrSize, INFO_TYPE_LteMacPeriodicBufferStatusReport);
        bsrInfo->ueRnti = LteLayer2GetRnti(node, interfaceIndex);
        bsrInfo->bufferSizeByte = bufferSize;
        bsrInfo->bufferSizeLevel = MacLteGetBsrLevel(bufferSize);
#ifdef LTE_LIB_LOG
        lte::LteLog::DebugFormat(node, interfaceIndex,
            LTE_STRING_LAYER_TYPE_MAC,
            "%s,Tx,BufferSize=,%d,%s=," LTE_STRING_FORMAT_RNTI,
            LTE_STRING_CATEGORY_TYPE_BSR,
            bsrInfo->bufferSizeByte,
            LTE_STRING_STATION_TYPE_ENB,
            dstRnti.nodeId, dstRnti.interfaceIndex);
#endif
    }
}

// Calculate Transport Block Size
//
// \param node                 : IN  Pointer to node.
// \param interfaceIndex       : IN  Interface index
// \param msg                  : IN  MAC PDU
//
// \return Transport Block Size
int MacLteCalculateTbs(
    Node* node, int interfaceIndex, UInt8 mcsIndex, int numRB)
{
    int phyIndex =
        LteGetPhyIndexFromMacInterfaceIndex(node, interfaceIndex);

    int numSfPerTti = MacLteGetNumSubframePerTti(node, interfaceIndex);
    int tbs = 0;
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);

    switch (stationType) {
        case LTE_STATION_TYPE_ENB:
        {
            // bit -> byte
            tbs = PhyLteGetDlTxBlockSize(
                node, phyIndex, mcsIndex, numRB) / 8;
            break;
        }
        case LTE_STATION_TYPE_UE:
        {
            tbs = PhyLteGetUlTxBlockSize(
                node, phyIndex, mcsIndex, numRB) / 8;
            break;
        }
        default:
        {
            char errBuf[MAX_STRING_LENGTH] = {0};
            sprintf(errBuf,
                "LTE do not support LteStationType(%d)\n"
                "Please check LteStationType.\n"
                , stationType);
            ERROR_ReportError(errBuf);
            break;
        }
    }
    return numSfPerTti * tbs;
}

// Print Statistics
//
// \param node                 : IN  Pointer to node.
// \param interfaceIndex       : IN  Interface index
// \param msg                  : IN  MAC PDU
//
// \return Transport Block Size
static void MacLtePrintStat(
    Node* node, int interfaceIndex, LteMacStatData* macStat)
{
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);

    char buf[MAX_STRING_LENGTH] = {0};


    if (stationType == LTE_STATION_TYPE_ENB)
    {
        sprintf(buf, "Number of receiving Random Access Preamble = %d",
            macStat->numberOfRecievingRaPreamble);
        IO_PrintStat(node,
                     "Layer2",
                     "LTE MAC",
                     ANY_DEST,
                     interfaceIndex,
                     buf);

        sprintf(buf, "Number of sending Random Access Grant = %d",
            macStat->numberOfSendingRaGrant);
        IO_PrintStat(node,
                     "Layer2",
                     "LTE MAC",
                     ANY_DEST,
                     interfaceIndex,
                     buf);
    }else{
        sprintf(buf, "Number of sending Random Access Preamble = %d",
            macStat->numberOfSendingRaPreamble);
        IO_PrintStat(node,
                     "Layer2",
                     "LTE MAC",
                     ANY_DEST,
                     interfaceIndex,
                     buf);

        sprintf(buf, "Number of receiving Random Access Grant = %d",
            macStat->numberOfRecievingRaGrant);
        IO_PrintStat(node,
                     "Layer2",
                     "LTE MAC",
                     ANY_DEST,
                     interfaceIndex,
                     buf);

        double averageCounter = 0;
        if (macStat->numberOfEstablishment > 0)
        {
            averageCounter =
                ((double)macStat->numberOfSendingRaPreamble /
                    macStat->numberOfEstablishment);
        }
        sprintf(buf, "Average count of PREAMBLE_TRANSMISSION_COUNTER = %lf",
            averageCounter);
        IO_PrintStat(node,
                     "Layer2",
                     "LTE MAC",
                     ANY_DEST,
                     interfaceIndex,
                     buf);

        double averageHarqDlReTx = 0;
        if (macStat->numberOfFirstReceiveTb > 0)
        {
            averageHarqDlReTx = ((double)macStat->numberOfReceiveTb - (double)macStat->numberOfFirstReceiveTb)
            / (double)macStat->numberOfFirstReceiveTb;
        }
        sprintf(buf, "Average number of downlink HARQ retransmission = %.3f",
            averageHarqDlReTx);
        IO_PrintStat(node,
                     "Layer2",
                     "LTE MAC",
                     ANY_DEST,
                     interfaceIndex,
                     buf);
        double averageHarqUlReTx = 0;
        if (macStat->numberOfFirstSendTb > 0)
        {
            averageHarqUlReTx = ((double)macStat->numberOfSendTb - (double)macStat->numberOfFirstSendTb)
            / (double)macStat->numberOfFirstSendTb;
        }
        sprintf(buf, "Average number of uplink HARQ retransmission = %.3f",
            averageHarqUlReTx);
        IO_PrintStat(node,
                     "Layer2",
                     "LTE MAC",
                     ANY_DEST,
                     interfaceIndex,
                     buf);
    }

    //-----------------------------------------------------------------------

    sprintf(buf, "Number of MAC SDU from Upper Layer = %d",
        macStat->numberOfSduFromUpperLayer);
    IO_PrintStat(node,
                 "Layer2",
                 "LTE MAC",
                 ANY_DEST,
                 interfaceIndex,
                 buf);

    sprintf(buf, "Number of MAC PDU to Lower Layer = %d",
        macStat->numberOfPduToLowerLayer);
    IO_PrintStat(node,
                 "Layer2",
                 "LTE MAC",
                 ANY_DEST,
                 interfaceIndex,
                 buf);

    sprintf(buf, "Number of MAC PDU from Lower Layer = %d",
        macStat->numberOfPduFromLowerLayer);
    IO_PrintStat(node,
                 "Layer2",
                 "LTE MAC",
                 ANY_DEST,
                 interfaceIndex,
                 buf);

    sprintf(buf, "Number of MAC SDU to Upper Layer = %d",
        macStat->numberOfSduToUpperLayer);
    IO_PrintStat(node,
                 "Layer2",
                 "LTE MAC",
                 ANY_DEST,
                 interfaceIndex,
                 buf);

    //-----------------------------------------------------------------------
}

// Add BSR
//
// \param node               : IN  Pointer to node.
// \param interfaceIndex     : IN  Interface index
// \param msg                : IN  MAC PDU
// \param tbIndex  int                        :
// \param dlSchedulingResult : IN  DL scheduling
//    result
//
void MacLteAddDciForDl(
    Node* node, int interfaceIndex, Message* msg,
    int tbIndex,
    LteDlSchedulingResultInfo* dlSchedulingResult,
    MacLteDlHarqTxProcess* txProcesses)
{
    // add DCI
    switch (dlSchedulingResult->txScheme)
    {
        case TX_SCHEME_SINGLE_ANTENNA:
        case TX_SCHEME_DIVERSITY:
        {
            // DCI Format 1
            LteDciFormat1 dci;
            dci.resAllocHeader = 0;
            dci.mcsID = dlSchedulingResult->mcsIndex[0];
            dci.harqProcessId = dlSchedulingResult->harqProcessId;
            dci.ndi = txProcesses->_tbStatus[0]->_NDI;
            dci.redundancyVersion = dlSchedulingResult->rvidx[0];
            memcpy(dci.usedRB_list, dlSchedulingResult->allocatedRb,
                sizeof(UInt8) * LTE_MAX_NUM_RB);
            char* info = MESSAGE_AddInfo(
                node, msg, sizeof(LteDciFormat1), INFO_TYPE_LteDci1Info);
            memcpy(info, &dci, sizeof(LteDciFormat1));
#ifdef ADDON_DB
            LteMacAppendStatsDBControlInfo(node,
                                           msg,
                                           sizeof(LteDciFormat1));
#endif
            break;
        }
        case TX_SCHEME_OL_SPATIAL_MULTI:
        {
            // DCI Format 2a
            LteDciFormat2a dci;
            dci.resAllocHeader = 0; // Fixed value
            memcpy(dci.usedRB_list, dlSchedulingResult->allocatedRb,
                sizeof(UInt8) * LTE_MAX_NUM_RB);

            // Note that, normally, transport block index equals to
            // codeword index.  If only transport block #0 is not created,
            // codeword#0 corresponds to transport block #1.
            dci.tb2cwSwapFlag = (tbIndex == 1);

            dci.forTB[0].mcsID = dlSchedulingResult->mcsIndex[0];
            dci.forTB[1].mcsID = dlSchedulingResult->mcsIndex[1];
            dci.harqProcessId = dlSchedulingResult->harqProcessId;
            dci.forTB[0].ndi = txProcesses->_tbStatus[0]->_NDI;
            dci.forTB[1].ndi = txProcesses->_tbStatus[1]->_NDI;
            dci.forTB[0].redundancyVersion = dlSchedulingResult->rvidx[0];
            dci.forTB[1].redundancyVersion = dlSchedulingResult->rvidx[1];

            char* info = MESSAGE_AddInfo(
                node, msg, sizeof(LteDciFormat2a), INFO_TYPE_LteDci2aInfo);
            memcpy(info, &dci, sizeof(LteDciFormat2a));
#ifdef ADDON_DB
            LteMacAppendStatsDBControlInfo(node,
                                           msg,
                                           sizeof(LteDciFormat2a));
#endif
            break;
        }
        default:
        {
            ERROR_ReportError("Unknown TX Mode!");
        }
    }

}

// Get number of Resource Blocks for DL
//
// \param dlSchedulingResult : IN  DL scheduling
//    result
//
// \return number of RBs
int MacLteGetNumberOfRbForDl(
    LteDlSchedulingResultInfo* dlSchedulingResult)
{
    int numRb = 0;
    UInt8* allocatedRb = dlSchedulingResult->allocatedRb;
    for (int i = 0; i < LTE_MAX_NUM_RB; i++)
    {
        numRb += allocatedRb[i] == 1 ? 1 : 0;
    }
    return numRb;
}

static void MacLteLogOutputForRlcBuffer(Node* node, int interfaceIndex)
{
#ifdef LTE_LIB_LOG
    ListLteRnti connectedList;
    Layer3LteGetConnectionList(node, interfaceIndex, &connectedList,
        LAYER3_LTE_CONNECTION_CONNECTED);
    ListLteRnti* list = &connectedList;
    std::vector < UInt32 > txBuffer, rtxBuffer;
    txBuffer.resize(list->size(), 0);
    rtxBuffer.resize(list->size(), 0);
    if (list != NULL)
    {
        ListLteRnti::iterator itr;
        int count = 0;
        for (itr  = list->begin();
            itr != list->end();
            ++itr, count++)
        {
            txBuffer.at(count) =
                LteRlcSendableByteInTxQueue(
                node, interfaceIndex,
                LteRnti(itr->nodeId, itr->interfaceIndex),
                LTE_DEFAULT_BEARER_ID);
            rtxBuffer.at(count) =
                LteRlcSendableByteInQueue(
                node, interfaceIndex,
                LteRnti(itr->nodeId, itr->interfaceIndex),
                LTE_DEFAULT_BEARER_ID) - txBuffer.at(count);

            lte::LteLog::Debug2Format(node, interfaceIndex,
                LTE_STRING_LAYER_TYPE_MAC,
                "TxRtxBuffer," LTE_STRING_FORMAT_RNTI ",%d,%d",
                itr->nodeId, itr->interfaceIndex,
                txBuffer.at(count), rtxBuffer.at(count));
        }
    }
#endif // LTE_LIB_LOG
}

static void MacLteLogOutputPeriodic(Node* node, int interfaceIndex)
{
#ifdef LTE_LIB_LOG
    MapLteConnectionInfo& mapConInfo =
        LteLayer2GetLayer3DataLte(node, interfaceIndex)->connectionInfoMap;
    MapLteConnectionInfo::iterator conItr;
    for (conItr  = mapConInfo.begin();
        conItr != mapConInfo.end();
        ++conItr)
    {
        const LteRnti& oppositeRnti = conItr->first;
        MapLteRadioBearer& mapRb = conItr->second.connectedInfo.radioBearers;
        MapLteRadioBearer::iterator rbItr;
        for (rbItr  = mapRb.begin();
            rbItr != mapRb.end();
            ++rbItr)
        {
            int rbId = rbItr->first;
            LteRadioBearer& rb = rbItr->second;
            // RRC

            // PDCP

            // RLC
            LteRlcEntity& rlcEntity = rb.rlcEntity;
            LteRlcLogPrintWindow(node, interfaceIndex, &rlcEntity);

            // MAC
        }
    }
#endif
}


// Get BSR Level from size[byte].
//
//    + size             : BSR size[byte].
//
// \return BSR Level
int MacLteGetBsrLevel(UInt32 size)
{
    UInt16 bufferLevel = 0;
    if (size == 0) bufferLevel = 0;
    else if (0 < size && size <= 10) bufferLevel = 1;
    else if (10 < size && size <= 12) bufferLevel = 2;
    else if (12 < size && size <= 14) bufferLevel = 3;
    else if (14 < size && size <= 17) bufferLevel = 4;
    else if (17 < size && size <= 19) bufferLevel = 5;
    else if (19 < size && size <= 22) bufferLevel = 6;
    else if (22 < size && size <= 26) bufferLevel = 7;
    else if (26 < size && size <= 31) bufferLevel = 8;
    else if (31 < size && size <= 36) bufferLevel = 9;
    else if (36 < size && size <= 42) bufferLevel = 10;
    else if (42 < size && size <= 49) bufferLevel = 11;
    else if (49 < size && size <= 57) bufferLevel = 12;
    else if (57 < size && size <= 67) bufferLevel = 13;
    else if (67 < size && size <= 78) bufferLevel = 14;
    else if (78 < size && size <= 91) bufferLevel = 15;
    else if (91 < size && size <= 107) bufferLevel = 16;
    else if (107 < size && size <= 125) bufferLevel = 17;
    else if (125 < size && size <= 146) bufferLevel = 18;
    else if (146 < size && size <= 171) bufferLevel = 19;
    else if (171 < size && size <= 200) bufferLevel = 20;
    else if (200 < size && size <= 234) bufferLevel = 21;
    else if (234 < size && size <= 274) bufferLevel = 22;
    else if (274 < size && size <= 321) bufferLevel = 23;
    else if (321 < size && size <= 376) bufferLevel = 24;
    else if (376 < size && size <= 440) bufferLevel = 25;
    else if (440 < size && size <= 515) bufferLevel = 26;
    else if (515 < size && size <= 603) bufferLevel = 27;
    else if (603 < size && size <= 706) bufferLevel = 28;
    else if (706 < size && size <= 826) bufferLevel = 29;
    else if (826 < size && size <= 967) bufferLevel = 30;
    else if (967 < size && size <= 1132) bufferLevel = 31;
    else if (1132 < size && size <= 1326) bufferLevel = 32;
    else if (1326 < size && size <= 1552) bufferLevel = 33;
    else if (1552 < size && size <= 1817) bufferLevel = 34;
    else if (1817 < size && size <= 2127) bufferLevel = 35;
    else if (2127 < size && size <= 2490) bufferLevel = 36;
    else if (2490 < size && size <= 2915) bufferLevel = 37;
    else if (2915 < size && size <= 3413) bufferLevel = 38;
    else if (3413 < size && size <= 3995) bufferLevel = 39;
    else if (3995 < size && size <= 4677) bufferLevel = 40;
    else if (4677 < size && size <= 5476) bufferLevel = 41;
    else if (5476 < size && size <= 6411) bufferLevel = 42;
    else if (6411 < size && size <= 7505) bufferLevel = 43;
    else if (7505 < size && size <= 8787) bufferLevel = 44;
    else if (8787 < size && size <= 10287) bufferLevel = 45;
    else if (10287 < size && size <= 12043) bufferLevel = 46;
    else if (12043 < size && size <= 14099) bufferLevel = 47;
    else if (14099 < size && size <= 16507) bufferLevel = 48;
    else if (16507 < size && size <= 19325) bufferLevel = 49;
    else if (19325 < size && size <= 22624) bufferLevel = 50;
    else if (22624 < size && size <= 26487) bufferLevel = 51;
    else if (26487 < size && size <= 31009) bufferLevel = 52;
    else if (31009 < size && size <= 36304) bufferLevel = 53;
    else if (36304 < size && size <= 42502) bufferLevel = 54;
    else if (42502 < size && size <= 49759) bufferLevel = 55;
    else if (49759 < size && size <= 58255) bufferLevel = 56;
    else if (58255 < size && size <= 68201) bufferLevel = 57;
    else if (68201 < size && size <= 79846) bufferLevel = 58;
    else if (79846 < size && size <= 93479) bufferLevel = 59;
    else if (93479 < size && size <= 109439) bufferLevel = 60;
    else if (109439 < size && size <= 128125) bufferLevel = 61;
    else if (128125 < size && size <= 150000) bufferLevel = 62;
    else bufferLevel = 63; // 150000 < size

    return bufferLevel;
}

// Set next TTI timer
//
// \param node               : IN  Pointer to node.
// \param interfaceIndex     : IN  Interface index
//
void MacLteSetNextTtiTimer(Node* node, int interfaceIndex)
{
    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);
    clocktype ttiLength = 0;
    int phyIndex =
        LteGetPhyIndexFromMacInterfaceIndex(node, interfaceIndex);

    switch (stationType)
    {
        case LTE_STATION_TYPE_ENB:
        {
            ttiLength = MacLteGetTtiLength(node, interfaceIndex);
            break;
        }
        case LTE_STATION_TYPE_UE:
        {
            // Maintenance of Uplink Time Alignment
            clocktype propDelay =
                PhyLteGetPropagationDelay(node, phyIndex);
            if ((macData->macState == MAC_LTE_RA_GRANT_WAITING ||
                macData->macState == MAC_LTE_RA_BACKOFF_WAITING) &&
                macData->lastPropDelay != propDelay)
            {
                ttiLength =
                    MacLteGetTtiLength(node, interfaceIndex) - 2 * propDelay;
            }
            else
            {
                ttiLength =
                    MacLteGetTtiLength(node, interfaceIndex);
            }
            break;
        }
        default:
        {
            char errBuf[MAX_STRING_LENGTH] = {0};
            sprintf(errBuf,
                "LTE do not support LteStationType(%d)\n"
                "Please check LteStationType.\n"
                , stationType);
            ERROR_ReportError(errBuf);
            break;
        }
    }

    MacLteSetTimerForMac(
        node, interfaceIndex, MSG_MAC_LTE_TtiTimerExpired, ttiLength);
}

// Set timer message
//
// \param node            : IN  Pointer to node.
// \param interfaceIndex  : IN  Interface index
// \param eventType       : IN  eventType in Message structure
// \param delay           : IN  delay before timer expired
//
void MacLteSetTimerForMac(
    Node* node, int interfaceIndex, int eventType, clocktype delay)
{
    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);

    Message* timerMsg =
        MESSAGE_Alloc(
            node, MAC_LAYER, MAC_PROTOCOL_LTE, eventType);
    MESSAGE_SetInstanceId(timerMsg, interfaceIndex);
    MESSAGE_Send(node, timerMsg, delay);

    macData->mapMacTimer[eventType] = timerMsg;

#ifdef LTE_LIB_LOG
    std::string name = GetLteTimerName(eventType);
    double msec = (double)delay / 1000000.0;
    lte::LteLog::Debug2Format(node, interfaceIndex,
        "MacLteSetTimerForMac",
        "SetTimer(name:delay[msec]), %s, %.3lf", name.c_str(), msec);
#endif

}

// Get timer message
//
// \param node            : IN  Pointer to node.
// \param interfaceIndex  : IN  Interface index
// \param eventType       : IN  eventType in Message structure
//
// \return Timer Message
Message* MacLteGetTimerForMac(
    Node* node, int interfaceIndex, int eventType)
{
    Message* retTimer = NULL;

    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);
    LteMapMessage* mapTimer = &(macData->mapMacTimer);
    LteMapMessage::iterator itr;
    itr = mapTimer->find(eventType);
    if (itr != mapTimer->end())
    {
        retTimer = (*itr).second;
    }

    return retTimer;
}

// Cancel timer
//
// \param node            : IN  Pointer to node.
// \param interfaceIndex  : IN  Interface index
// \param eventType       : IN  eventType in Message structure
// \param timerMsg        : IN  Pointer to timer message
//
void MacLteCancelTimerForMac(
    Node* node, int interfaceIndex, int eventType, Message* timerMsg)
{
    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);
    MESSAGE_CancelSelfMsg(node, timerMsg);
    timerMsg = NULL;
    LteMapMessage* mapMacTimer = &(macData->mapMacTimer);
    LteMapMessage::iterator itr;
    itr = mapMacTimer->find(eventType);
    if (itr == mapMacTimer->end())
    {
#ifdef LTE_LIB_LOG
        lte::LteLog::FatalFormat(node, interfaceIndex,
            "MacLteCancelTimerForMac", __FILE__, __LINE__,
            "%s is not set or already canceled.",
            GetLteTimerName(eventType).c_str());
#endif
        char errBuf[MAX_STRING_LENGTH] = {0};
        sprintf(errBuf,
            "Timer(EventType=%d) is not set or already canceled.",
            eventType);
        ERROR_ReportError(errBuf);
    }
    mapMacTimer->erase(eventType);
}

// Set state
//
// \param node               : IN  Pointer to node.
// \param interfaceIndex     : IN  Interface index
// \param state              : IN  State
//
void MacLteSetState(Node* node, int interfaceIndex, MacLteState state)
{
    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);

#ifdef LTE_LIB_LOG
    std::string before = GetLteMacStateName(macData->macState);
    std::string after  = GetLteMacStateName(state);
    lte::LteLog::InfoFormat(node, interfaceIndex,
        LTE_STRING_LAYER_TYPE_MAC,
        "%s,%s=,%s,%s=,%s",
        LTE_STRING_CATEGORY_TYPE_SET_STATE,
        LTE_STRING_COMMON_TYPE_BEFORE, before.c_str(),
        LTE_STRING_COMMON_TYPE_AFTER, after.c_str());
#endif

    macData->macState = state;

}

////////////////////////////////////////////////////////////////////////////
// eNB/UE - API for Common
////////////////////////////////////////////////////////////////////////////
// Get TTI Number
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface Index
//
// \return TTI Number
UInt64 MacLteGetTtiNumber(Node* node, int interfaceIndex)
{
    return LteLayer2GetLteMacData(node, interfaceIndex)->ttiNumber;
}

// Set TTI Number
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface Index
// \param ttiNumber  TTI Number
//
void MacLteSetTtiNumber(Node* node, int interfaceIndex, UInt64 ttiNumber)
{
    LteLayer2GetLteMacData(node, interfaceIndex)->ttiNumber = ttiNumber;
}

// Get number of subframe per TTI
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface Index
//
// \return Number of subframe per TTI
int MacLteGetNumSubframePerTti(
    Node* node, int interfaceIndex)
{
    return LteLayer2GetLayer2DataLte(node, interfaceIndex)->
        numSubframePerTti;
}

// Get TTI length [nsec]
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface Index
//
// \return TTI length
clocktype MacLteGetTtiLength(
    Node* node, int interfaceIndex)
{
    return MacLteGetNumSubframePerTti(node, interfaceIndex) * MILLI_SECOND;
}

////////////////////////////////////////////////////////////////////////////
// eNB/UE - API for PHY
////////////////////////////////////////////////////////////////////////////
// Receive a Transport Block from PHY Layer
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface Index
// \param srcRnti  Source Node's RNTI
// \param transportBlockMsg  one Transport Block
//
void MacLteReceiveTransportBlockFromPhy(
    Node* node, int interfaceIndex, LteRnti srcRnti,
    Message* transportBlockMsg)
{
    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);

    if (node->guiOption)
    {
        GUI_Receive(
            srcRnti.nodeId,
            node->nodeId,
            GUI_PHY_LAYER,
            GUI_DEFAULT_DATA_TYPE,
            srcRnti.interfaceIndex,
            interfaceIndex,
            node->getNodeTime());
    }

    int tbs = MESSAGE_ReturnPacketSize(transportBlockMsg);
    ERROR_Assert(tbs > 0, "Size of transport block is 0.");

    macData->statData.numberOfPduFromLowerLayer++;
#ifdef ADDON_DB
    LteStatsDbSduPduInfo* receiveMsgDBInfo = (LteStatsDbSduPduInfo*)
               MESSAGE_ReturnInfo(transportBlockMsg,
                                 (UInt16)INFO_TYPE_LteStatsDbSduPduInfo);
    HandleMacDBEvents(
        node,
        transportBlockMsg,
        node->macData[interfaceIndex]->phyNumber,
        interfaceIndex,
        MAC_ReceiveFromPhy,
        node->macData[interfaceIndex]->macProtocol);

    if (node->macData[interfaceIndex]->macStats)
    {
        node->macData[interfaceIndex]->stats->
               AddFrameReceivedDataPoints(node,
                                          transportBlockMsg,
                                          STAT_Unicast,
                                          receiveMsgDBInfo->ctrlSize,
                                          receiveMsgDBInfo->dataSize,
                                          interfaceIndex);
    }
#endif
    std::list<Message*> sduList;
    MacLteDoDemultiplexing(
        node, interfaceIndex, transportBlockMsg, sduList);
#ifdef LTE_LIB_HO_VALIDATION
    if (LteLayer2GetStationType(node, interfaceIndex)
        == LTE_STATION_TYPE_UE)
    {
        const LteRnti& enbRnti =
            Layer3LteGetConnectedEnb(node, interfaceIndex);
        LteConnectionInfo* connInfo =
            Layer3LteGetConnectionInfo(node, interfaceIndex, enbRnti);
        std::list< Message* >::iterator itr;
        for (itr = sduList.begin();
            itr != sduList.end();
            ++itr)
        {
            connInfo->tempRecvByte +=
                MESSAGE_ReturnPacketSize((*itr));
        }
    }
#endif

    macData->statData.numberOfSduToUpperLayer +=
                    (unsigned int)sduList.size();
    LteRlcReceivePduFromMac(
        node, interfaceIndex, srcRnti, LTE_DEFAULT_BEARER_ID, sduList);
}


////////////////////////////////////////////////////////////////////////////
// eNB/UE - API for RRC
////////////////////////////////////////////////////////////////////////////
// SAP for Power ON Notification from RRC Layer
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface Index
//
void MacLteNotifyPowerOn(Node* node, int interfaceIndex)
{
#ifdef LTE_LIB_LOG
    lte::LteLog::InfoFormat(node, interfaceIndex,
        LTE_STRING_LAYER_TYPE_MAC,
        "%s",
        LTE_STRING_CATEGORY_TYPE_POWER_ON);
#endif


    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);
    switch (stationType)
    {
        case LTE_STATION_TYPE_ENB:
        {
            MacLteSetState(node, interfaceIndex, MAC_LTE_DEFAULT_STATUS);
            break;
        }
        case LTE_STATION_TYPE_UE:
        {
            MacLteSetState(node, interfaceIndex, MAC_LTE_IDEL);
            break;
        }
        default:
        {
            char errBuf[MAX_STRING_LENGTH] = {0};
            sprintf(errBuf,
                "LTE do not support LteStationType(%d)\n"
                "Please check LteStationType.\n"
                , stationType);
            ERROR_ReportError(errBuf);
            break;
        }
    }

    // Notify layer2 scheduler power on
    LteLayer2GetScheduler(node, interfaceIndex)->notifyPowerOn();
}

// SAP for Power ON Notification from RRC Layer
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface Index
//
void MacLteNotifyPowerOff(Node* node, int interfaceIndex)
{
    MacLteSetState(node, interfaceIndex, MAC_LTE_POWER_OFF);

    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);
    LteMacData* macData =
        LteLayer2GetLteMacData(node, interfaceIndex);
    LteMapMessage* mapMacTimer = &(macData->mapMacTimer);
    LteMapMessage::iterator itr;
    for (itr  = mapMacTimer->begin();
        itr != mapMacTimer->end();
        ++itr)
    {
        // do not cancel TTI Timer only eNB
        if ((*itr).first == MSG_MAC_LTE_TtiTimerExpired &&
            stationType == LTE_STATION_TYPE_ENB)
        {
            continue;
        }
        MESSAGE_CancelSelfMsg(node, (*itr).second);
        (*itr).second = NULL;
    }

    mapMacTimer->clear();
    macData->lastPropDelay = 0;
    macData->preambleTransmissionCounter = 1;

    // delete HARQ TxEntity
    if (macData->dlHarqTxEntity)
    {
        std::map<LteRnti, MacLteDlHarqTxEntity*>::iterator harqIte;
        harqIte = macData->dlHarqTxEntity->begin();
        while (harqIte != macData->dlHarqTxEntity->end())
        {
            delete harqIte->second;
            ++harqIte;
        }

        macData->dlHarqTxEntity->clear();
    }
    if (macData->ulHarqRxEntity)
    {
        std::map<LteRnti, MacLteUlHarqRxEntity*>::iterator harqIte;
        harqIte = macData->ulHarqRxEntity->begin();
        while (harqIte != macData->ulHarqRxEntity->end())
        {
            delete harqIte->second;
            ++harqIte;
        }
        macData->ulHarqRxEntity->clear();
    }

    if (macData->dlHarqRxEntity)
    {
        macData->dlHarqRxEntity->Clear();
    }

    if (macData->ulHarqTxEntity)
    {
        macData->ulHarqTxEntity->Clear();
    }

    // Notify layer2 scheduler power on
    LteLayer2GetScheduler(node, interfaceIndex)->notifyPowerOff();
}

// /**
// FUNCTION   :: MacLteNotifyRlcReset
// LAYER      :: MAC
// PURPOSE    :: Process notification of RLC reset.
//               HARQ retransmission should be terminated.
// PARAMETERS ::
// + node      : Node*            : Pointer to node.
// + phyIndex  : int              : Index of the PHY
// + rnti      : const LteRnti&   : opposite RNTI
// RETURN     :: void  : NULL
// **/
void MacLteNotifyRlcReset(Node* node, int interfaceIndex, const LteRnti& oppositeRnti)
{
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);

    LteMacData* macData = LteLayer2GetLteMacData(node, interfaceIndex);

    if (stationType == LTE_STATION_TYPE_ENB)
    {
        {
            std::map<LteRnti, MacLteDlHarqTxEntity*>::iterator harqIte;
            harqIte = macData->dlHarqTxEntity->find(oppositeRnti);
            if (harqIte != macData->dlHarqTxEntity->end())
            {
                harqIte->second->TerminateRetransmission();
            }
        }
        {
            std::map<LteRnti, MacLteUlHarqRxEntity*>::iterator harqIte;
            harqIte = macData->ulHarqRxEntity->find(oppositeRnti);
            if (harqIte != macData->ulHarqRxEntity->end())
            {
                harqIte->second->TerminateRetransmission();
            }
        }
    }else
    {
        ;
    }
}


////////////////////////////////////////////////////////////////////////////
// eNB - API for PHY
////////////////////////////////////////////////////////////////////////////
// SAP for BSR Notification from eNB's PHY LAYER
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface Index
// \param bsrInfo  BSR Info Structure
//
void MacLteNotifyBsrFromPhy(
    Node* node, int interfaceIndex, const LteBsrInfo& bsrInfo)
{
    Layer3LteNotifyBsrFromMac(node, interfaceIndex, bsrInfo);

#ifdef LTE_LIB_LOG
    lte::LteLog::DebugFormat(node, interfaceIndex,
        LTE_STRING_LAYER_TYPE_MAC,
        "%s,Rx,BufferSize=,%d,%s=," LTE_STRING_FORMAT_RNTI,
        LTE_STRING_CATEGORY_TYPE_BSR,
        bsrInfo.bufferSizeByte,
        LTE_STRING_STATION_TYPE_UE,
        bsrInfo.ueRnti.nodeId, bsrInfo.ueRnti.interfaceIndex);
#endif
}

////////////////////////////////////////////////////////////////////////////
// eNB/UE - API for Scheduler
////////////////////////////////////////////////////////////////////////////
// Clear MAC layer contexts related to specified node(RNTI)
//
// \param node  Pointer to node.
// \param interfaceIndex  Interface Index
// \param rnti const  RNTI
void MacLteClearInfo(Node* node, int interfaceIndex, const LteRnti& rnti)
{
    LteStationType stationType =
        LteLayer2GetStationType(node, interfaceIndex);
    LteMacData* macData =
        LteLayer2GetLteMacData(node, interfaceIndex);
    LteMapMessage* mapMacTimer = &(macData->mapMacTimer);
    LteMapMessage::iterator itr;
    for (itr  = mapMacTimer->begin();
        itr != mapMacTimer->end();
        ++itr)
    {
        // do not cancel TTI Timer only eNB
        if ((*itr).first == MSG_MAC_LTE_TtiTimerExpired &&
            stationType == LTE_STATION_TYPE_ENB)
        {
            continue;
        }
        MESSAGE_CancelSelfMsg(node, (*itr).second);
        (*itr).second = NULL;
    }

    mapMacTimer->clear();
    macData->lastPropDelay = 0;
    macData->preambleTransmissionCounter = 1;

    // delete HARQ TxEntity
    if (macData->dlHarqTxEntity)
    {
        std::map<LteRnti, MacLteDlHarqTxEntity*>::iterator harqIte;
        harqIte = macData->dlHarqTxEntity->find(rnti);
        if (harqIte != macData->dlHarqTxEntity->end())
        {
            delete harqIte->second;
            macData->dlHarqTxEntity->erase(harqIte);
        }
    }

    if (macData->ulHarqRxEntity)
    {
        std::map<LteRnti, MacLteUlHarqRxEntity*>::iterator ulHarqIte;
        ulHarqIte = macData->ulHarqRxEntity->find(rnti);
        if (ulHarqIte != macData->ulHarqRxEntity->end())
        {
            delete ulHarqIte->second;
            macData->ulHarqRxEntity->erase(ulHarqIte);
        }
    }

    if (macData->dlHarqRxEntity)
    {
        macData->dlHarqRxEntity->Clear();
    }

    if (macData->ulHarqTxEntity)
    {
        macData->ulHarqTxEntity->Clear();
    }

    // Notify layer2 scheduler power on
    LteLayer2GetScheduler(node, interfaceIndex)->notifyPowerOff();
}