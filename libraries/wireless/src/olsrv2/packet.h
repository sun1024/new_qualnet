// Copyright (c) 2001-2015, SCALABLE Network Technologies, Inc.  All Rights Reserved.
//                          600 Corporate Pointe
//                          Suite 1200
//                          Culver City, CA 90230
//                          info@scalable-networks.com
//
// This source code is licensed, not sold, and is subject to a written
// license agreement.  Among other things, no portion of this source
// code may be copied, transmitted, disclosed, displayed, distributed,
// translated, used as the basis for a derivative work, or used, in
// whole or in part, for any program or purpose other than its intended
// use in compliance with the license agreement as part of the QualNet
// software.  This source code and certain of the algorithms contained
// within it are confidential trade secrets of Scalable Network
// Technologies, Inc. and may not be used as the basis for any other
// software, hardware, product or service.

/*
 *
 * Copyright (c) 2006, Graduate School of Niigata University,
 *                                         Ad hoc Network Lab.
 * Developer:
 *  Yasunori Owada  [yowada@net.ie.niigata-u.ac.jp],
 *  Kenta Tsuchida  [ktsuchi@net.ie.niigata-u.ac.jp],
 *  Taka Maeno      [tmaeno@net.ie.niigata-u.ac.jp],
 *  Hiroei Imai     [imai@ie.niigata-u.ac.jp].
 * Contributor:
 *  Keita Yamaguchi [kyama@net.ie.niigata-u.ac.jp],
 *  Yuichi Murakami [ymura@net.ie.niigata-u.ac.jp],
 *  Hiraku Okada    [hiraku@ie.niigata-u.ac.jp].
 *
 * This software is available with usual "research" terms
 * with the aim of retain credits of the software.
 * Permission to use, copy, modify and distribute this software for any
 * purpose and without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies,
 * and the name of NIIGATA, or any contributor not be used in advertising
 * or publicity pertaining to this material without the prior explicit
 * permission. The software is provided "as is" without any
 * warranties, support or liabilities of any kind.
 * This product includes software developed by the University of
 * California, Berkeley and its contributors protected by copyrights.
 */
#ifndef OLSR_PLUGIN
#ifndef _OLSR_PACKET
#define _OLSR_PACKET

#include "olsr_protocol.h"
#include "olsr_conf.h"

typedef struct local_interface_block_entry {
  union olsr_ip_addr iface_addr;
  olsr_u8_t          other_if;
}OLSR_LOCAL_INTERFACE_BLOCK_ENTRY;

struct hello_neighbor
{
  olsr_u8_t             status;
  olsr_u8_t             is_mpr;
  olsr_u8_t             other_if;
  olsr_u8_t            other_neigh;


  /*
    olsr_u8_t             link;
    double                link_quality;
    double                neigh_link_quality;
    union olsr_ip_addr    main_address;
  */

  union olsr_ip_addr    address;//advertized addr
  struct hello_neighbor *next;
};

struct hello_message
{
  double                 vtime;
  double                 htime;
  union olsr_ip_addr     source_addr;
  union olsr_ip_addr     orig_addr;
  olsr_u16_t             message_seq_number;
  olsr_u8_t              hop_count;
  olsr_u8_t              ttl;
  olsr_u8_t              willingness;

  OLSR_LIST local_iface_list;
  struct hello_neighbor  *neighbors;

};

struct tc_mpr_addr
{
  /*
    double             link_quality;
    double             neigh_link_quality;
  */
  /*
    address tlv ...
  */
  union olsr_ip_addr address;


  struct tc_mpr_addr *next;
};

struct tc_message
{
  double              vtime;
  union olsr_ip_addr  source_addr;
  union olsr_ip_addr  originator;
  olsr_u16_t          message_seq_number;
  olsr_u8_t           hop_count;
  olsr_u8_t           ttl;
  olsr_u16_t          assn;

  OLSR_LIST local_iface_list;
  struct tc_mpr_addr  *mpr_selector_address;
  ATTACHED_NETWORK    *attached_net_addr;
};

struct unknown_message
{
  olsr_u16_t          seqno;
  union olsr_ip_addr originator;
  olsr_u8_t          type;
};


void
olsr_free_hello_packet(struct hello_message *);

int
olsr_build_hello_packet(struct hello_message *,
            union olsr_ip_addr *);

void
olsr_free_tc_packet(struct tc_message *);

int
olsr_build_tc_packet(struct tc_message *);

#endif
#endif
