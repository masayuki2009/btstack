############################################################################
# apps/external/btstack/Makefile
#
#   Copyright (C) 2018 Masayuki Ishikawa. All rights reserved.
#   Author: Masayuki Ishikawa <masayuki.ishikawa@gmail.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name NuttX nor the names of its contributors may be
#    used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
# OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
############################################################################

-include $(TOPDIR)/Make.defs

# Btstack_Demo, World! built-in application info

CONFIG_EXAMPLES_BTSTACK_PRIORITY ?= SCHED_PRIORITY_DEFAULT
CONFIG_EXAMPLES_BTSTACK_STACKSIZE ?= 2048

APPNAME   = btapp
PRIORITY  = $(CONFIG_EXAMPLES_BTSTACK_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_BTSTACK_STACKSIZE)

#

BTSTACK_ROOT = .

CFLAGS += -I${BTSTACK_ROOT}/chipset/csr
CFLAGS += -I${BTSTACK_ROOT}/port/posix-h4
CFLAGS += -I${BTSTACK_ROOT}/src
CFLAGS += -I${BTSTACK_ROOT}/src/classic
CFLAGS += -I${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include -D OI_DEBUG
CFLAGS += -I${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include
CFLAGS += -I${BTSTACK_ROOT}/3rd-party/tinydir
CFLAGS += -I${BTSTACK_ROOT}/platform/posix
CFLAGS += -I${BTSTACK_ROOT}/platform/embedded
CFLAGS += -I..
CFLAGS += -DNUTTX -DCSR_ONLY -DCSR8811_PSKEYS

ASRCS   =

CSRCS   = ./src/ad_parser.c
CSRCS  += ./src/btstack_memory.c
CSRCS  += ./src/btstack_linked_list.c
CSRCS  += ./src/btstack_run_loop.c
CSRCS  += ./src/btstack_slip.c
CSRCS  += ./src/btstack_util.c
CSRCS  += ./src/classic/bnep.c
CSRCS  += ./src/classic/sdp_client.c
CSRCS  += ./src/classic/sdp_util.c
CSRCS  += ./src/hci.c
CSRCS  += ./src/hci_cmd.c
CSRCS  += ./src/hci_dump.c
CSRCS  += ./src/hci_transport_h4.c
CSRCS  += ./src/l2cap.c
CSRCS  += ./src/l2cap_signaling.c
CSRCS  += ./platform/posix/btstack_link_key_db_fs.c
CSRCS  += ./platform/posix/btstack_network_posix.c
CSRCS  += ./platform/posix/btstack_run_loop_posix.c
CSRCS  += ./platform/posix/btstack_uart_block_posix.c
CSRCS  += ./chipset/csr/btstack_chipset_csr.c

#CSRCS  += ./example/gap_inquiry.c
CSRCS  += ./example/panu_demo.c

MAINSRC = port/posix-h4/main.c

CONFIG_EXAMPLES_BTSTACK_PROGNAME ?= btapp$(EXEEXT)
PROGNAME = $(CONFIG_EXAMPLES_BTSTACK_PROGNAME)

include $(APPDIR)/Application.mk
