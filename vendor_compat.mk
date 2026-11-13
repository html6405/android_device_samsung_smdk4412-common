# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

_smdk4412_vendor_compat_local_path := $(LOCAL_PATH)
LOCAL_PATH := vendor/samsung/smdk4412-common
include vendor/samsung/smdk4412-common/smdk4412-common-vendor.mk

# System paths resolve into the vendor namespace; GLES2 is now built from source.
PRODUCT_COPY_FILES := $(filter-out \
    %:system/lib/egl/libGLESv1_CM_mali.so \
    %:system/lib/egl/libGLESv2_mali.so \
    %:vendor/lib/egl/libGLESv2_mali.so, \
    $(PRODUCT_COPY_FILES))

LOCAL_PATH := $(_smdk4412_vendor_compat_local_path)
_smdk4412_vendor_compat_local_path :=
