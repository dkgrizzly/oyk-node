# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder

# Classic Macintosh (System 6.0.8 - Mac OS 9.2.2) Makefile
# Builds 68k, PowerPC CFM, and Combined Fat Binary (.bin MacBinary + .dsk HFS) via Retro68.

RETRO68_PREFIX ?= $(HOME)/opt/Retro68
CXX68K         := $(RETRO68_PREFIX)/bin/m68k-apple-macos-g++
CXXPPC         := $(RETRO68_PREFIX)/bin/powerpc-apple-macos-g++
REZ            := $(RETRO68_PREFIX)/bin/Rez
MAKEPEF        := $(RETRO68_PREFIX)/bin/MakePEF
REZ_INC        := -I$(RETRO68_PREFIX)/RIncludes -I$(RETRO68_PREFIX)/multiversal/RIncludes

BUILD_DIR      ?= build_macclassic
COMPAT_DIR     := ../compat

CXXFLAGS_COMMON := -std=c++98 -Os -fno-exceptions -fno-rtti -Wall -Wextra \
                   -ffunction-sections -fdata-sections -DOYK_TARGET_MACCLASSIC=1 -I. -I$(COMPAT_DIR)

CORE_SRCS := \
	$(COMPAT_DIR)/oyk_portable.cpp \
	$(COMPAT_DIR)/oyk_yapp.cpp \
	$(COMPAT_DIR)/oyk_rig.cpp \
	$(COMPAT_DIR)/oyk_printer.cpp \
	$(COMPAT_DIR)/oyk_term.cpp \
	$(COMPAT_DIR)/oyk_gui.cpp \
	$(COMPAT_DIR)/oyk_gui_theme.cpp

APP_SRCS := \
	mac_main.cpp \
	mac_ui.cpp \
	mac_net.cpp \
	mac_capture.cpp \
	$(CORE_SRCS)

TEST_SRCS := \
	test_macclassic.cpp \
	mac_net.cpp \
	mac_capture.cpp \
	$(CORE_SRCS)

APP_OBJS_68K := $(patsubst %.cpp,$(BUILD_DIR)/68k/%.o,$(notdir $(APP_SRCS)))
APP_OBJS_PPC := $(patsubst %.cpp,$(BUILD_DIR)/ppc/%.o,$(notdir $(APP_SRCS)))
TEST_OBJS    := $(patsubst %.cpp,$(BUILD_DIR)/test/%.o,$(notdir $(TEST_SRCS)))

.PHONY: all app test fat clean

all: fat test

app: $(BUILD_DIR)/OYKNode-Fat.bin

fat: $(BUILD_DIR)/OYKNode-Fat.bin

test: $(BUILD_DIR)/OYKTest.bin

$(BUILD_DIR)/68k:
	@mkdir -p $(BUILD_DIR)/68k $(BUILD_DIR)/ppc $(BUILD_DIR)/test

define BUILD_RULES_68K
$(BUILD_DIR)/68k/$(basename $(notdir $(1))).o: $(1) | $(BUILD_DIR)/68k
	$(CXX68K) $(CXXFLAGS_COMMON) -c $(1) -o $$@
endef
$(foreach s,$(APP_SRCS),$(eval $(call BUILD_RULES_68K,$(s))))

define BUILD_RULES_PPC
$(BUILD_DIR)/ppc/$(basename $(notdir $(1))).o: $(1) | $(BUILD_DIR)/68k
	$(CXXPPC) $(CXXFLAGS_COMMON) -c $(1) -o $$@
endef
$(foreach s,$(APP_SRCS),$(eval $(call BUILD_RULES_PPC,$(s))))

define BUILD_RULES_TEST
$(BUILD_DIR)/test/$(basename $(notdir $(1))).o: $(1) | $(BUILD_DIR)/68k
	$(CXX68K) $(CXXFLAGS_COMMON) -c $(1) -o $$@
endef
$(foreach s,$(TEST_SRCS),$(eval $(call BUILD_RULES_TEST,$(s))))

$(BUILD_DIR)/OYKNode68k.code.bin: $(APP_OBJS_68K)
	$(CXX68K) -Wl,-gc-sections -Wl,--mac-strip-macsbug $(APP_OBJS_68K) -o $@

$(BUILD_DIR)/OYKNodePPC.xcoff: $(APP_OBJS_PPC)
	$(CXXPPC) -Wl,-gc-sections $(APP_OBJS_PPC) -o $@

$(BUILD_DIR)/OYKNodePPC.pef: $(BUILD_DIR)/OYKNodePPC.xcoff
	$(MAKEPEF) $< -o $@

$(BUILD_DIR)/OYKNode-Fat.bin: $(BUILD_DIR)/OYKNode68k.code.bin $(BUILD_DIR)/OYKNodePPC.pef oyk_mac.r
	$(REZ) -DOYK_FAT_BINARY $(REZ_INC) oyk_mac.r \
		--copy $(BUILD_DIR)/OYKNode68k.code.bin \
		--data $(BUILD_DIR)/OYKNodePPC.pef \
		-t APPL -c OYKN \
		-o $(BUILD_DIR)/OYKNode-Fat.bin \
		--cc $(BUILD_DIR)/OYKNode-Fat.dsk

$(BUILD_DIR)/OYKTest.code.bin: $(TEST_OBJS)
	$(CXX68K) -Wl,-gc-sections -Wl,--mac-strip-macsbug $(TEST_OBJS) -o $@

$(BUILD_DIR)/OYKTest.bin: $(BUILD_DIR)/OYKTest.code.bin oyk_mac.r
	$(REZ) $(REZ_INC) oyk_mac.r \
		--copy $(BUILD_DIR)/OYKTest.code.bin \
		-t APPL -c OYKT \
		-o $(BUILD_DIR)/OYKTest.bin \
		--cc $(BUILD_DIR)/OYKTest.dsk

clean:
	rm -rf $(BUILD_DIR)
