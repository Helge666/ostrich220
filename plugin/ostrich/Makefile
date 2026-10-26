#!/usr/bin/make -f
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Ostrich, the U-220 plugin.  DPF is plugin/dpf with our patches (../patches); the core is
# plugin/core/u220_core.cpp, built by ../tools/build_u220_core.sh into a static library.
#
#   make          Linux: VST3, CLAP, standalone (JACK)
#   make win      Windows, cross-compiled with mingw-w64: VST3, CLAP, standalone

NAME = Ostrich

FILES_DSP = OstrichPlugin.cpp
FILES_UI  = OstrichUI.cpp

include ../dpf/Makefile.plugins.mk

CORE_LIB ?= ../build-u220/libu220core.a

BUILD_CXX_FLAGS := -I. -I../core $(BUILD_CXX_FLAGS) -std=c++20
LINK_FLAGS      += $(abspath $(CORE_LIB))

ifeq ($(WINDOWS),true)
EXTRA_LIBS      += -lpthread
endif

TARGETS = vst3 clap jack

all:
	@$(MAKE) --no-print-directory patches
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
	@$(MAKE) --no-print-directory patches
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

# DPF is a submodule and we do not fork it, so the changes we need travel as patches
# applied at build time (../patches; each says what it is for).  Idempotent: --check tells
# whether one is already in.  *.pugl.patch go to DPF's own pugl submodule, the rest to
# DPF.  A patch newly applied throws DGL's objects away: its build does not see a change in
# a file pugl.cpp includes (the file dialog, patch 0013), and the old library would be
# linked as it was.
patches:
	@for p in ../patches/*.patch; do \
		case "$$p" in *.pugl.patch) d=../dpf/dgl/src/pugl-upstream ;; *) d=../dpf ;; esac; \
		if git -C $$d apply --check --reverse "$(CURDIR)/$$p" 2>/dev/null; then \
			:; \
		elif git -C $$d apply "$(CURDIR)/$$p" 2>/dev/null; then \
			echo "  applied $$(basename $$p) to $$d"; \
			rm -rf ../dpf/build*/dgl ../dpf/build*/libdgl*.a; \
		else \
			echo "  WARNING: $$(basename $$p) does not apply to $$d -- check the submodule"; \
		fi; \
	done

.PHONY: patches
