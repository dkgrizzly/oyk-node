# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

MINTELF_PREFIX ?= $(HOME)/opt/mintelf
CXX := $(MINTELF_PREFIX)/usr/bin/m68k-atari-mintelf-g++
COMPAT_DIR := ../compat
BUILD_DIR := build_atarist

CXXFLAGS := -m68000 -std=c++98 -Os -fno-exceptions -fno-rtti \
            -Wall -Wextra -D__ATARI__=1 -I. -I$(COMPAT_DIR)
LDFLAGS := -m68000 -lgem

COMPAT_SRCS := \
	$(COMPAT_DIR)/oyk_portable.cpp \
	$(COMPAT_DIR)/oyk_yapp.cpp \
	$(COMPAT_DIR)/oyk_rig.cpp \
	$(COMPAT_DIR)/oyk_printer.cpp \
	$(COMPAT_DIR)/oyk_term.cpp \
	$(COMPAT_DIR)/oyk_gui.cpp \
	$(COMPAT_DIR)/oyk_gui_theme.cpp

COMMON_SRCS := atari_policy.cpp atari_net.cpp atari_capture.cpp atari_ui.cpp $(COMPAT_SRCS)

.PHONY: all clean

all: $(BUILD_DIR)/OYKNODE.PRG $(BUILD_DIR)/OYKTEST.PRG

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/OYKNODE.PRG: atari_main.cpp $(COMMON_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/OYKTEST.PRG: test_atarist.cpp $(COMMON_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
