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
#ifndef __OLSR_UTIL_H
#define __OLSR_UTIL_H

//WIN FIX
//#include<sys/time.h>
#include "olsr_types.h"
#include "olsr_common.h"

#include "olsr.h"

#if defined __cplusplus
extern "C" {
#endif

char * olsr_niigata_ip_to_string(struct olsrv2* olsr, union olsr_ip_addr *);
void qualnet_print_clock_in_second(qualnet_time_t, char []);

void ConvertIpv4AddressToString(olsr_u32_t ipAddress,char *addressString);
void ConvertIpv6AddressToString(in6_addr* ipv6Address,
                                char* interfaceAddr,
                                olsr_bool ipv4EmbeddeAddr);
#if defined __cplusplus
}
#endif

olsr_bool equal_netmask(struct olsrv2* olsr,const union hna_netmask *, const union hna_netmask *);
olsr_32_t cmp_ip_addr(struct olsrv2* olsr, const union olsr_ip_addr *, const union olsr_ip_addr *);

char* convert_to_char_from_time(time_t time);
void set_current_time(struct olsrv2 *, olsr_time_t *);
void get_current_time(struct olsrv2 *, olsr_time_t *);
void update_time(olsr_time_t *, olsr_32_t);
olsr_time_t olsr_max_time(olsr_time_t, olsr_time_t);
olsr_time_t olsr_min_time(olsr_time_t, olsr_time_t);
L_STATUS get_link_status(olsr_time_t, olsr_time_t, olsr_time_t, olsr_time_t);
L_STATUS get_neighbor_status(olsr_time_t *, olsr_time_t *);


#endif
