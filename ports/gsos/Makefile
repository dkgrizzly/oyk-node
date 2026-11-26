# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

VBCC_PREFIX ?= $(HOME)/opt/vbcc
VC := VBCC=$(VBCC_PREFIX) PATH=$(VBCC_PREFIX)/bin:$(PATH) vc +iigs
VLINK := $(VBCC_PREFIX)/bin/vlink
CFLAGS := -O1 -size -D__VBCC__=1 -I.
BUILD_DIR := build_gsos
VLINK_FLAGS := -stack=8192 -maxsegsize=16384 -mtype -version=2 -m -b appleomf -Cvbcc -L$(VBCC_PREFIX)/targets/65816-iigs/lib $(VBCC_PREFIX)/targets/65816-iigs/lib/startup.o

COMMON_OBJS := $(BUILD_DIR)/gsos_ui.o $(BUILD_DIR)/gsos_net.o $(BUILD_DIR)/gsos_capture.o $(BUILD_DIR)/gsos_core.o

.PHONY: all clean

all: $(BUILD_DIR)/OYKNODE.SYS16 $(BUILD_DIR)/OYKTEST.SYS16

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c gsos_core.h gsos_net.h gsos_ui.h gsos_capture.h | $(BUILD_DIR)
	$(VC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/OYKNODE.SYS16: $(BUILD_DIR)/gsos_main.o $(COMMON_OBJS)
	$(VLINK) $(VLINK_FLAGS) $^ -o $@ -lvc

$(BUILD_DIR)/OYKTEST.SYS16: $(BUILD_DIR)/test_gsos.o $(COMMON_OBJS)
	$(VLINK) $(VLINK_FLAGS) $^ -o $@ -lvc

clean:
	rm -rf $(BUILD_DIR)
