#!/usr/bin/make -f
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Ostrich, the U-220 plugin.  Shares plugin/dpf (with its patches) and plugin/compat with
# Voltaire 110; the core is plugin/core/u220_core.cpp, built by ../tools/build_u220_core.sh
# into a static library, exactly as ../Makefile does it for the U-110.
#
#   make          Linux: VST3, CLAP, standalone (JACK)
#   make win      Windows, cross-compiled with mingw-w64: VST3, CLAP, standalone

NAME = Ostrich

FILES_DSP = OstrichPlugin.cpp
FILES_UI  = OstrichUI.cpp

include ../dpf/Makefile.plugins.mk

CORE_LIB ?= ../build-u220/libu220core.a

# -I. first, so DPF finds OUR DistrhoPluginInfo.h and not Voltaire's in ../src.
BUILD_CXX_FLAGS := -I. -I../core $(BUILD_CXX_FLAGS) -std=c++20
LINK_FLAGS      += $(abspath $(CORE_LIB))

ifeq ($(WINDOWS),true)
EXTRA_LIBS      += -lpthread
endif

TARGETS = vst3 clap jack

all:
	@$(MAKE) --no-print-directory -C .. patches
	@../tools/build_u220_core.sh
	@for f in bin/$(NAME).clap bin/$(NAME) bin/$(NAME).vst3/Contents/x86_64-linux/$(NAME).so; do \
		if [ -e "$$f" ] && [ "$(CORE_LIB)" -nt "$$f" ]; then rm -f "$$f"; fi; done
	@$(MAKE) --no-print-directory $(TARGETS)

CROSS_PREFIX ?= x86_64-w64-mingw32-
WIN_BUILD    = $(CURDIR)/build-win
WIN_BIN      = $(CURDIR)/bin-win
WIN_PRODUCTS = $(WIN_BIN)/$(NAME).clap \
               $(WIN_BIN)/$(NAME).vst3/Contents/x86_64-win/$(NAME).vst3 \
               $(WIN_BIN)/$(NAME).exe

win:
	@$(MAKE) --no-print-directory -C .. patches
	@U220_CORE_BUILD=$(WIN_BUILD)/core CXX=$(CROSS_PREFIX)g++ AR=$(CROSS_PREFIX)ar \
		../tools/build_u220_core.sh
	@for f in $(WIN_PRODUCTS); do \
		if [ -e "$$f" ] && [ "$(WIN_BUILD)/core/libu220core.a" -nt "$$f" ]; then rm -f "$$f"; fi; done
	@$(MAKE) --no-print-directory clap vst3 jack \
		CC=$(CROSS_PREFIX)gcc CXX=$(CROSS_PREFIX)g++ AR=$(CROSS_PREFIX)ar \
		BUILD_DIR_SUFFIX=-win \
		DPF_BUILD_DIR=$(WIN_BUILD)/$(NAME) \
		DPF_TARGET_DIR=$(WIN_BIN) \
		CORE_LIB=$(WIN_BUILD)/core/libu220core.a
	@file $(WIN_PRODUCTS)

.PHONY: all win
