# Copyright 2013 The Android Open Source Project

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CFLAGS := -Werror
ifeq ($(strip $(BOARD_CHARGER_DISABLE_INIT_BLANK)),true)
LOCAL_CFLAGS += -DCHARGER_DISABLE_INIT_BLANK
endif
ifeq ($(strip $(BOARD_CHARGER_ENABLE_SUSPEND)),true)
LOCAL_CFLAGS += -DCHARGER_ENABLE_SUSPEND
endif

LOCAL_SRC_FILES := \
    healthd_mode_charger.cpp \
    AnimationParser.cpp

LOCAL_MODULE := libhealthd_charger_exynos4
LOCAL_C_INCLUDES := bootable/recovery $(LOCAL_PATH)/include
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/include

LOCAL_STATIC_LIBRARIES := \
    android.hardware.health@2.0 \
    android.hardware.health@2.0-impl \
    android.hardware.health@1.0 \
    android.hardware.health@1.0-convert \
    libhealthstoragedefault \
    libminui \
    libpng \
    libz \
    libutils \
    libbase \
    libcutils \
    libhealthd_draw \
    liblog \
    libm \
    libc \

ifeq ($(strip $(BOARD_CHARGER_ENABLE_SUSPEND)),true)
LOCAL_STATIC_LIBRARIES += libsuspend
endif

include $(BUILD_STATIC_LIBRARY)

### charger ###
include $(CLEAR_VARS)
ifeq ($(strip $(BOARD_CHARGER_NO_UI)),true)
LOCAL_CHARGER_NO_UI := true
endif
ifdef BRILLO
LOCAL_CHARGER_NO_UI := true
endif

LOCAL_SRC_FILES := \
    charger.cpp \

LOCAL_MODULE := charger_exynos4
LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_CFLAGS := -Werror
ifeq ($(strip $(LOCAL_CHARGER_NO_UI)),true)
LOCAL_CFLAGS += -DCHARGER_NO_UI
endif

CHARGER_STATIC_LIBRARIES := \
    android.hardware.health@2.0-impl \
    android.hardware.health@2.0 \
    android.hardware.health@1.0 \
    android.hardware.health@1.0-convert \
    libhidlbase \
    libhealthstoragedefault \
    libvndksupport \
    libhealthd_charger_exynos4 \
    libhealthd_draw \
    libbatterymonitor \
    libbase \
    libutils \
    libcutils \
    liblog \
    libm \
    libc \

LOCAL_STATIC_LIBRARIES := $(CHARGER_STATIC_LIBRARIES)

ifneq ($(strip $(LOCAL_CHARGER_NO_UI)),true)
LOCAL_STATIC_LIBRARIES += \
    libminui \
    libpng \
    libz \

endif

ifeq ($(strip $(BOARD_CHARGER_ENABLE_SUSPEND)),true)
LOCAL_STATIC_LIBRARIES += libsuspend
endif

#LOCAL_STATIC_LIBRARIES += libhealthd

# Symlink /charger to /sbin/charger
LOCAL_POST_INSTALL_CMD := $(hide) ln -sf /system/bin/charger_exynos4 /system/bin/charger

#include $(BUILD_EXECUTABLE)
