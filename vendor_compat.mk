# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

# Include the generated vendor configuration here so its copy rules can be
# adjusted without modifying generated files or proprietary binaries.
_smdk4412_vendor_compat_local_path := $(LOCAL_PATH)
LOCAL_PATH := vendor/samsung/smdk4412-common
include vendor/samsung/smdk4412-common/smdk4412-common-vendor.mk

# The fixed system paths must resolve to vendor files for the sphal namespace.
# Android.bp installs symlinks instead of these duplicate system copies.
PRODUCT_COPY_FILES := $(filter-out \
    %:system/lib/egl/libGLESv1_CM_mali.so \
    %:system/lib/egl/libGLESv2_mali.so, \
    $(PRODUCT_COPY_FILES))

LOCAL_PATH := $(_smdk4412_vendor_compat_local_path)
_smdk4412_vendor_compat_local_path :=
