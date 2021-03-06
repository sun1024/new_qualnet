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

CBR 4 3 100 512 1S 5S 0S PRECEDENCE 0
FTP/GENERIC 1 3 50 512 10 0S 
MCBR 1 225.0.0.0 100 512 1S 5S 0S PRECEDENCE 0
FTP 1 2 10 50S
VBR 1 2 512 1S 2S 0S
HTTPD 6
HTTPD 5
HTTPD 2
HTTP 1 3 6 5 2 10S 120S
TRAFFIC-GEN 4 3 DET 10 DET 100 RND UNI 250 250 UNI 20MS 20MS 1 NOLB
SUPER-APPLICATION 1 3 DELIVERY-TYPE UNRELIABLE START-TIME DET 6S DURATION DET 200S REQUEST-NUM DET 50 REQUEST-SIZE DET 512  REQUEST-INTERVAL DET 1MS REQUEST-TOS PRECEDENCE 0 REPLY-PROCESS NO