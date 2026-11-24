# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

# OpenWatcom v2 / 1.9 WMAKE Makefile for OYK Packet Node (MS-DOS / IBM XT 8088 / NEC V20)
#
# Works on both Linux hosts (open-watcom-v2 in /opt/watcom or ~/open-watcom-v2/rel)
# and DOS/Windows hosts.
#
# Prerequisites:
#   - OpenWatcom v2 (or 1.9) with WATCOM set (e.g. export WATCOM=/opt/watcom)
#   - Michael Brutman's mTCP cloned to ~/mTCP (upstream git repo has src/TCPINC and src/TCPLIB)
#     Only required for oyknode.exe; testdos.exe is standalone.
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

# Upstream mTCP git repo places TCPINC/, TCPLIB/, and INCLUDE/ under src/
!ifndef MTCP_SRC
MTCP_SRC = $(MTCP_DIR)/src
!endif

# -0 = 8086/8088 (IBM PC/XT); override with CPU=-1 for NEC V20
!ifndef CPU
CPU = -0
!endif

CC  = wpp
AS  = wasm
LD  = wlink

CFLAGS_COMMON = $(CPU) -ml -os -bt=dos -wx -zq -I../compat

# On case-sensitive Linux/Unix hosts, mTCP headers are uppercase on disk (TYPES.H, GLOBAL.CFG)
# while mTCP sources #include lowercase/mixed-case names ("types.h", "Global.Cfg").
# We generate a local mtcp_inc/ symlink directory automatically without modifying MTCP_DIR.
!ifdef __UNIX__
MTCP_INC_FLAGS = -I. -Imtcp_inc -I$(MTCP_SRC)/TCPINC -I$(MTCP_SRC)/INCLUDE
CFG_DEF        = '-DCFG_H="oyk_mtcp.cfg"'
!else
MTCP_INC_FLAGS = -I. -I$(MTCP_SRC)/TCPINC -I$(MTCP_SRC)/INCLUDE
CFG_DEF        = -DCFG_H="oyk_mtcp.cfg"
!endif

CFLAGS_MTCP     = $(CPU) -ml -os -bt=dos -zq $(CFG_DEF) $(MTCP_INC_FLAGS) -I../compat
CFLAGS_MTCP_LIB = $(CPU) -ml -os -s -zp2 -bt=dos -zq $(CFG_DEF) $(MTCP_INC_FLAGS)

NODE_OBJS = dos_main.obj ems.obj oyk_portable.obj
TEST_OBJS = test_dos.obj test_portable_dos.obj ems.obj oyk_portable.obj

MTCP_OBJS = &
    mtcp_packet.obj &
    mtcp_arp.obj &
    mtcp_eth.obj &
    mtcp_ip.obj &
    mtcp_udp.obj &
    mtcp_tcp.obj &
    mtcp_tcpsockm.obj &
    mtcp_timer.obj &
    mtcp_utils.obj &
    mtcp_trace.obj &
    mtcp_ipasm.obj

all: oyknode.exe testdos.exe

!ifdef __UNIX__
mtcp_inc_stamp: prep_mtcp_inc.py
    python3 prep_mtcp_inc.py "$(MTCP_SRC)" mtcp_inc
    touch mtcp_inc_stamp
!else
mtcp_inc_stamp: .SYMBOLIC
    @%null
!endif

dos_main.obj: mtcp_inc_stamp dos_main.cpp oyk_mtcp.cfg ems.hpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_MTCP) -fo=dos_main.obj dos_main.cpp

ems.obj: ems.cpp ems.hpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_COMMON) -fo=ems.obj ems.cpp

oyk_portable.obj: ../compat/oyk_portable.cpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_COMMON) -fo=oyk_portable.obj ../compat/oyk_portable.cpp

test_dos.obj: test_dos.cpp ems.hpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_COMMON) -fo=test_dos.obj test_dos.cpp

test_portable_dos.obj: ../../tests/test_portable.cpp ../compat/oyk_portable.hpp
    $(CC) $(CFLAGS_COMMON) -DOYK_PORTABLE_TESTS_AS_LIBRARY -fo=test_portable_dos.obj ../../tests/test_portable.cpp

# mTCP TCPLIB objects compiled from upstream uppercase sources with oyk_mtcp.cfg
mtcp_packet.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/PACKET.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_packet.obj $(MTCP_SRC)/TCPLIB/PACKET.CPP

mtcp_arp.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/ARP.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_arp.obj $(MTCP_SRC)/TCPLIB/ARP.CPP

mtcp_eth.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/ETH.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_eth.obj $(MTCP_SRC)/TCPLIB/ETH.CPP

mtcp_ip.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/IP.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_ip.obj $(MTCP_SRC)/TCPLIB/IP.CPP

mtcp_udp.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/UDP.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_udp.obj $(MTCP_SRC)/TCPLIB/UDP.CPP

mtcp_tcp.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/TCP.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_tcp.obj $(MTCP_SRC)/TCPLIB/TCP.CPP

mtcp_tcpsockm.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/TCPSOCKM.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_tcpsockm.obj $(MTCP_SRC)/TCPLIB/TCPSOCKM.CPP

mtcp_timer.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/TIMER.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_timer.obj $(MTCP_SRC)/TCPLIB/TIMER.CPP

mtcp_utils.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/UTILS.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_utils.obj $(MTCP_SRC)/TCPLIB/UTILS.CPP

mtcp_trace.obj: mtcp_inc_stamp oyk_mtcp.cfg $(MTCP_SRC)/TCPLIB/TRACE.CPP
    $(CC) $(CFLAGS_MTCP_LIB) -fo=mtcp_trace.obj $(MTCP_SRC)/TCPLIB/TRACE.CPP

mtcp_ipasm.obj: $(MTCP_SRC)/TCPLIB/IPASM.ASM
    $(AS) $(CPU) -ml -zq -fo=mtcp_ipasm.obj $(MTCP_SRC)/TCPLIB/IPASM.ASM

oyknode.exe: $(NODE_OBJS) $(MTCP_OBJS)
    $(LD) system dos option map,eliminate,stack=8192 name oyknode.exe file { $(NODE_OBJS) $(MTCP_OBJS) }

testdos.exe: $(TEST_OBJS)
    $(LD) system dos option map,stack=8192 name testdos.exe file { $(TEST_OBJS) }

clean: .SYMBOLIC
!ifdef __UNIX__
    rm -rf mtcp_inc mtcp_inc_stamp *.o *.obj *.err *.map oyknode.exe testdos.exe
!else
    del *.obj *.err *.map oyknode.exe testdos.exe
!endif
