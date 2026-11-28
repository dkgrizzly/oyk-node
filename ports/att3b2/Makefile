# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

CC ?= /usr/local/bin/gcc
CFLAGS ?= -O -Wall
CPPFLAGS = -DOYK_SYSV_SVR32 -I../sysv
NETLIBS = -lnet -lnsl_s
CURSESLIBS = -lcurses $(NETLIBS)

SYSV_OBJS = sysv_core.o sysv_tui.o sysv_net.o

all: oyk-node-3b2 oyk-3b2-test

sysv_core.o: ../sysv/sysv_core.c ../sysv/sysv_core.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c ../sysv/sysv_core.c -o $@

sysv_tui.o: ../sysv/sysv_tui.c ../sysv/sysv_tui.h ../sysv/sysv_logo_data.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c ../sysv/sysv_tui.c -o $@

sysv_net.o: ../sysv/sysv_net.c ../sysv/sysv_net.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c ../sysv/sysv_net.c -o $@

att3b2_tui_main.o: att3b2_tui_main.c ../sysv/sysv_core.h ../sysv/sysv_tui.h ../sysv/sysv_net.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c att3b2_tui_main.c -o $@

test_3b2_native.o: test_3b2_native.c ../sysv/sysv_core.h ../sysv/sysv_tui.h ../sysv/sysv_net.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c test_3b2_native.c -o $@

oyk-node-3b2: $(SYSV_OBJS) att3b2_tui_main.o
	$(CC) $(CFLAGS) -o $@ $(SYSV_OBJS) att3b2_tui_main.o $(CURSESLIBS)

oyk-3b2-test: $(SYSV_OBJS) test_3b2_native.o
	$(CC) $(CFLAGS) -o $@ $(SYSV_OBJS) test_3b2_native.o $(NETLIBS)

test: oyk-3b2-test oyk-node-3b2
	./oyk-3b2-test
	./oyk-node-3b2 --unit-test

clean:
	rm -f *.o oyk-node-3b2 oyk-3b2-test
