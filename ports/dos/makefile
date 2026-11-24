# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

# OpenWatcom v2 / 1.9 WMAKE Makefile for OYK Packet Node (MS-DOS / IBM XT 8088 / NEC V20)
#
# Works on both Linux hosts (open-watcom-v2 binl64/binl) and DOS/Windows hosts.
#
# Prerequisites:
#   - OpenWatcom v2 (or 1.9) with WATCOM set (e.g. export WATCOM=$HOME/open-watcom-v2/rel)
#   - Michael Brutman's mTCP cloned to ~/mTCP (only required for oyknode.exe; testdos.exe is standalone)
#
# Usage:
#   wmake -f makefile testdos.exe                       (build 16-bit DOS unit tests)
#   wmake -f makefile oyknode.exe MTCP_DIR=$HOME/mTCP   (build full DOS packet node)
#   wmake -f makefile all MTCP_DIR=$HOME/mTCP CPU=-1    (NEC V20 / 80186 optimization)

!ifndef MTCP_DIR
!ifdef %HOME
MTCP_DIR = $(%HOME)/mTCP
!else
MTCP_DIR = ../../third_party/mtcp
!endif
!endif

# -0 = 8086/8088 (IBM PC/XT); override with CPU=-1 for NEC V20
!ifndef CPU
CPU = -0
!endif

CC  = wpp
LD  = wlink

CFLAGS_COMMON = $(CPU) -ml -os -bt=dos -wx -zq -I../compat
CFLAGS_MTCP   = $(CFLAGS_COMMON) -I$(MTCP_DIR)/TCPINC

NODE_OBJS = dos_main.obj ems.obj oyk_portable.obj
TEST_OBJS = test_dos.obj test_portable_dos.obj ems.obj oyk_portable.obj

MTCP_OBJS = &
    $(MTCP_DIR)/TCPLIB/packet.obj &
    $(MTCP_DIR)/TCPLIB/arp.obj &
    $(MTCP_DIR)/TCPLIB/eth.obj &
    $(MTCP_DIR)/TCPLIB/ip.obj &
    $(MTCP_DIR)/TCPLIB/udp.obj &
    $(MTCP_DIR)/TCPLIB/tcp.obj &
    $(MTCP_DIR)/TCPLIB/tcpsockm.obj &
    $(MTCP_DIR)/TCPLIB/dns.obj &
    $(MTCP_DIR)/TCPLIB/timer.obj &
    $(MTCP_DIR)/TCPLIB/utils.obj &
    $(MTCP_DIR)/TCPLIB/ipasm.obj

all: oyknode.exe testdos.exe

dos_main.obj: dos_main.cpp ems.hpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_MTCP) dos_main.cpp

ems.obj: ems.cpp ems.hpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_COMMON) ems.cpp

oyk_portable.obj: ../compat/oyk_portable.cpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_COMMON) -fo=oyk_portable.obj ../compat/oyk_portable.cpp

test_dos.obj: test_dos.cpp ems.hpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_COMMON) test_dos.cpp

test_portable_dos.obj: ../../tests/test_portable.cpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_COMMON) -DOYK_PORTABLE_TESTS_AS_LIBRARY -fo=test_portable_dos.obj ../../tests/test_portable.cpp

oyknode.exe: $(NODE_OBJS)
    $(LD) system dos option map,stack=8192 name oyknode.exe file { $(NODE_OBJS) $(MTCP_OBJS) }

testdos.exe: $(TEST_OBJS)
    $(LD) system dos option map,stack=8192 name testdos.exe file { $(TEST_OBJS) }

clean: .SYMBOLIC
!ifdef __UNIX__
    rm -f *.obj *.err *.map oyknode.exe testdos.exe
!else
    del *.obj *.err *.map oyknode.exe testdos.exe
!endif
