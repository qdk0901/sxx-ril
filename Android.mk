# Copyright 2006 The Android Open Source Project

# XXX using libutils for simulator build only...
#
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    atchannel.c \
    misc.c \
    at_tok.c \
		sxx-ril.c \
    sxx-ril-pdp.c \
    sxx-ril-manager.c \
    sxx-ril-network.c \
    sxx-ril-message.c \
    sxx-ril-call.c \
    sxx-ril-service.c \
    sxx-ril-device.c \
    sxx-ril-sim.c \
    sxx-ril-wm218.c \
    sxx-ril-dm6600.c \
    helper/gsm.c \
    helper/sms.c \
    helper/sms_gsm.c


LOCAL_PRELINK_MODULE :=false
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
    libcutils libutils libril

# for asprinf
LOCAL_CFLAGS := -D_GNU_SOURCE

LOCAL_C_INCLUDES := $(KERNEL_HEADERS)

#USE HCRADIO
ifeq ($(BOARD_USES_HC_RADIO),true) 
LOCAL_CFLAGS += -DHCRADIO
endif

ifeq ($(TARGET_DEVICE),sooner)
  LOCAL_CFLAGS += -DOMAP_CSMI_POWER_CONTROL -DUSE_TI_COMMANDS
endif

ifeq ($(TARGET_DEVICE),surf)
  LOCAL_CFLAGS += -DPOLL_CALL_STATE -DUSE_QMI
endif

ifeq ($(TARGET_DEVICE),dream)
  LOCAL_CFLAGS += -DPOLL_CALL_STATE -DUSE_QMI
endif

ifeq (foo,foo)
  #build shared library
  LOCAL_SHARED_LIBRARIES += \
      libcutils libutils
  LOCAL_LDLIBS += -lpthread
  LOCAL_CFLAGS += -DRIL_SHLIB
  LOCAL_MODULE:= libreference-ril-sxx
  include $(BUILD_SHARED_LIBRARY)
else
  #build executable
  LOCAL_SHARED_LIBRARIES += \
      libril
  LOCAL_MODULE:= reference-ril
  include $(BUILD_EXECUTABLE)
endif