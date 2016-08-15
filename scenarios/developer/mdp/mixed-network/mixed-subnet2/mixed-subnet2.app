# Copyright (c) 2001-2015, SCALABLE Network Technologies, Inc.  All Rights Reserved.
#                          600 Corporate Pointe
#                          Suite 1200
#                          Culver City, CA 90230
#                          info@scalable-networks.com
#
# This source code is licensed, not sold, and is subject to a written
# license agreement.  Among other things, no portion of this source
# code may be copied, transmitted, disclosed, displayed, distributed,
# translated, used as the basis for a derivative work, or used, in
# whole or in part, for any program or purpose other than its intended
# use in compliance with the license agreement as part of the QualNet
# software.  This source code and certain of the algorithms contained
# within it are confidential trade secrets of Scalable Network
# Technologies, Inc. and may not be used as the basis for any other
# software, hardware, product or service.


CBR 1 7 0 512 1S 10S 90S PRECEDENCE 0 MDP-ENABLED
SUPER-APPLICATION 5 2 DELIVERY-TYPE UNRELIABLE START-TIME DET 10S DURATION DET 90S REQUEST-NUM DET 0 REQUEST-SIZE DET 512 REQUEST-INTERVAL DET 1S REQUEST-TOS PRECEDENCE 0 REPLY-PROCESS NO MDP-ENABLED
TRAFFIC-GEN 3 7 DET 10S DET 90S RND UNI 512 512 UNI 1S 1S 1 NOLB MDP-ENABLED
TRAFFIC-TRACE 7 1 DET 10S DET 90S TRC soccer.trc LB 30000 500000 DELAY MDP-ENABLED


MCBR 1 225.0.0.0 100 512 1S 10S 90S MDP-ENABLED MDP-PROFILE mdp-profile2
SUPER-APPLICATION 2 225.0.0.0 DELIVERY-TYPE UNRELIABLE START-TIME DET 10S DURATION DET 90S REQUEST-NUM DET 100 REQUEST-SIZE DET 512  REQUEST-INTERVAL DET 1S REQUEST-TOS PRECEDENCE 0 REPLY-PROCESS NO MDP-ENABLED
TRAFFIC-GEN 6 225.0.0.1 DET 10S DET 90S RND UNI 512 512 UNI 1S 1S 1 NOLB MDP-ENABLED MDP-PROFILE mdp-profile1
TRAFFIC-TRACE 7 225.0.0.0 DET 10S DET 90S TRC soccer.trc LB 30000 500000 DELAY MDP-ENABLED