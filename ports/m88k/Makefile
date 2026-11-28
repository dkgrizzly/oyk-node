# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

CC = /usr/ccs/bin/cc
CFLAGS = -O
CPPFLAGS = -DOYK_SYSV_SVR4 -I../sysv
NETLIBS = -lsocket -lnsl
CURSESLIBS = -lcurses $(NETLIBS)
MOTIFLIBS = -lXm -lXt -lX11 -lgen $(NETLIBS)

SYSV_SRCS = ../sysv/sysv_core.c ../sysv/sysv_tui.c ../sysv/sysv_net.c
SYSV_OBJS = sysv_core.o sysv_tui.o sysv_net.o

all: oyk-node-m88k oyk-node-m88k-motif oyk-m88k-test

sysv_core.o: ../sysv/sysv_core.c ../sysv/sysv_core.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c ../sysv/sysv_core.c -o $@

sysv_tui.o: ../sysv/sysv_tui.c ../sysv/sysv_tui.h ../sysv/sysv_logo_data.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c ../sysv/sysv_tui.c -o $@

sysv_net.o: ../sysv/sysv_net.c ../sysv/sysv_net.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c ../sysv/sysv_net.c -o $@

sysv_motif_ui.o: ../sysv/sysv_motif_ui.c ../sysv/sysv_motif_ui.h ../sysv/sysv_logo_data.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DOYK_HAS_MOTIF -c ../sysv/sysv_motif_ui.c -o $@

m88k_tui_main.o: m88k_tui_main.c ../sysv/sysv_core.h ../sysv/sysv_tui.h ../sysv/sysv_net.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c m88k_tui_main.c -o $@

m88k_motif_main.o: m88k_motif_main.c ../sysv/sysv_core.h ../sysv/sysv_motif_ui.h ../sysv/sysv_net.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DOYK_HAS_MOTIF -c m88k_motif_main.c -o $@

test_m88k_native.o: test_m88k_native.c ../sysv/sysv_core.h ../sysv/sysv_tui.h ../sysv/sysv_net.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c test_m88k_native.c -o $@

oyk-node-m88k: $(SYSV_OBJS) m88k_tui_main.o
	$(CC) $(CFLAGS) -o $@ $(SYSV_OBJS) m88k_tui_main.o $(CURSESLIBS)

oyk-node-m88k-motif: $(SYSV_OBJS) sysv_motif_ui.o m88k_motif_main.o
	$(CC) $(CFLAGS) -o $@ $(SYSV_OBJS) sysv_motif_ui.o m88k_motif_main.o $(MOTIFLIBS)

oyk-m88k-test: $(SYSV_OBJS) test_m88k_native.o
	$(CC) $(CFLAGS) -o $@ $(SYSV_OBJS) test_m88k_native.o $(NETLIBS)

test: oyk-m88k-test oyk-node-m88k
	./oyk-m88k-test
	./oyk-node-m88k --unit-test

clean:
	rm -f *.o oyk-node-m88k oyk-node-m88k-motif oyk-m88k-test
