# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

CC ?= cc
TMS9900_CC ?= tms9900-gcc
XAS99 ?= xas99.py

CFLAGS_HOST = -std=c99 -Wall -Wextra -Wpedantic -Werror -Os
CFLAGS_TI99 = -Os -std=c99 -fno-builtin -fno-function-cse -fno-peephole2 -D__TMS9900__

SRCS = ti99_core.c ti99_sams.c ti99_vdp.c ti99_tui.c ti99_tipi.c ti99_rs232.c ti99_main.c

all: oyk-ti99-host

oyk-ti99-host: $(SRCS)
	$(CC) $(CFLAGS_HOST) $(SRCS) -o $@

cart: ti99_cart_hdr.asm $(SRCS)
	$(XAS99) -R -b ti99_cart_hdr.asm -o oyk_hdr.bin
	$(TMS9900_CC) $(CFLAGS_TI99) -c ti99_core.c -o ti99_core.o
	$(TMS9900_CC) $(CFLAGS_TI99) -c ti99_sams.c -o ti99_sams.o
	$(TMS9900_CC) $(CFLAGS_TI99) -c ti99_vdp.c -o ti99_vdp.o
	$(TMS9900_CC) $(CFLAGS_TI99) -c ti99_tui.c -o ti99_tui.o
	$(TMS9900_CC) $(CFLAGS_TI99) -c ti99_tipi.c -o ti99_tipi.o
	$(TMS9900_CC) $(CFLAGS_TI99) -c ti99_rs232.c -o ti99_rs232.o
	$(TMS9900_CC) $(CFLAGS_TI99) -c ti99_main.c -o ti99_main.o
	$(TMS9900_CC) $(CFLAGS_TI99) *.o -o oyk-ti99.elf

test: oyk-ti99-host
	./oyk-ti99-host --unit-test
	./oyk-ti99-host --headless

clean:
	rm -f *.o oyk-ti99-host oyk-ti99.elf oyk_hdr.bin
