# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

AMIGA_GCC_PREFIX ?= $(HOME)/opt/amiga-gcc
CXX := $(AMIGA_GCC_PREFIX)/bin/m68k-amigaos-g++
COMPAT_DIR := ../compat
BUILD_DIR := build_amiga

CXXFLAGS := -m68000 -noixemul -std=c++98 -Os -fno-exceptions -fno-rtti \
            -Wall -Wextra -D__AMIGA__=1 -I. -I$(COMPAT_DIR)
LDFLAGS := -m68000 -noixemul -lm

COMPAT_SRCS := \
	$(COMPAT_DIR)/oyk_portable.cpp \
	$(COMPAT_DIR)/oyk_yapp.cpp \
	$(COMPAT_DIR)/oyk_rig.cpp \
	$(COMPAT_DIR)/oyk_printer.cpp \
	$(COMPAT_DIR)/oyk_term.cpp \
	$(COMPAT_DIR)/oyk_gui.cpp \
	$(COMPAT_DIR)/oyk_gui_theme.cpp

COMMON_SRCS := amiga_policy.cpp amiga_net.cpp amiga_capture.cpp amiga_ui.cpp $(COMPAT_SRCS)

.PHONY: all clean

all: $(BUILD_DIR)/OYKNode $(BUILD_DIR)/OYKTest

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/OYKNode: amiga_main.cpp $(COMMON_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/OYKTest: test_amiga.cpp $(COMMON_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
