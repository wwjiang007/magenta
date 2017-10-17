# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := test

MODULE_SRCS := \
    $(wildcard $(LOCAL_DIR)/*/*.c) \
    $(wildcard $(LOCAL_DIR)/*/*.cpp) \
    $(wildcard $(LOCAL_DIR)/*/*.S) \
    $(LOCAL_DIR)/main.c \

MODULE_NAME := core-tests

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/runtime \
    system/ulib/sync \

# This is a hack to deal with these functions needing to be compiled with
# -fno-stack-protector
MODULE_STATIC_LIBS += system/utest/core/threads/test-threads

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/mini-process \
    system/ulib/magenta \
    system/ulib/c

MODULE_DEFINES := BUILD_COMBINED_TESTS=1

# core/channel needs a header file generated by kernel/lib/vdso/rules.mk.
MODULE_COMPILEFLAGS += -I$(BUILDDIR)/kernel/lib/vdso
MODULE_SRCDEPS += $(BUILDDIR)/kernel/lib/vdso/vdso-code.h

include make/module.mk

MODULES += $(patsubst %/rules.mk,%,$(wildcard $(LOCAL_DIR)/*/rules.mk))
