#
# Copyright (C) 2012 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

DEVICE_PATH := device/samsung/smdk4412-common

# Wifi
WIFI_DRIVER_MODULE_PATH :=

# Bionic
MALLOC_SVELTE := true
TARGET_NEEDS_PLATFORM_TEXT_RELOCATIONS := true
LIBART_IMG_BASE := 0x30000000

# Misc hacks
BUILD_BROKEN_DUP_RULES := true
BUILD_BROKEN_USES_BUILD_COPY_HEADERS := true
BUILD_BROKEN_USES_BUILD_HOST_STATIC_LIBRARY := true
BUILD_BROKEN_USES_BUILD_HOST_EXECUTABLE := true
TEMPORARY_DISABLE_PATH_RESTRICTIONS := true
TARGET_OTA_ALLOW_NON_AB := true
BUILD_BROKEN_MISSING_REQUIRED_MODULES := true
RELAX_USES_LIBRARY_CHECK := true
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true
#BUILD_BROKEN_PHONY_TARGETS := true

# HIDL
DEVICE_MATRIX_FILE := $(DEVICE_PATH)/compatibility_matrix.xml
PRODUCT_VENDOR_MOVE_ENABLED := true
BUILD_BROKEN_VINTF_PRODUCT_COPY_FILES := true
PRODUCT_ENFORCE_VINTF_MANIFEST_OVERRIDE := true


# Memfd
TARGET_HAS_MEMFD_BACKPORT := true

# This variable is set first, so it can be overridden
# by BoardConfigVendor.mk
USE_CAMERA_STUB := false
BOARD_USES_GENERIC_AUDIO := false
USE_XML_AUDIO_POLICY_CONF := 1

TARGET_BOOTANIMATION_PRELOAD := true

TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_SMP := true
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_ARCH_VARIANT_CPU := cortex-a9
TARGET_CPU_VARIANT := cortex-a9
ARCH_ARM_HAVE_NEON := true
ARCH_ARM_HAVE_TLS_REGISTER := true
TARGET_USES_GRALLOC1 := true
TARGET_USES_64_BIT_BINDER := true
BOARD_CANT_REALLOCATE_OMX_BUFFERS := true

BOARD_VENDOR := samsung
TARGET_BOARD_PLATFORM := exynos4
TARGET_SOC := exynos4x12
TARGET_BOOTLOADER_BOARD_NAME := smdk4x12

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

TARGET_PROVIDES_INIT := true
TARGET_PROVIDES_INIT_TARGET_RC := true

# Kernel
BOARD_KERNEL_CMDLINE := console=ttySAC2,115200
BOARD_KERNEL_IMAGE_NAME := zImage
BOARD_KERNEL_BASE := 0x40000000
BOARD_KERNEL_PAGESIZE := 2048
#KERNEL_TOOLCHAIN := prebuilts/gcc/$(HOST_OS)-x86/arm/arm-eabi-4.8/bin
#TARGET_KERNEL_CROSS_COMPILE_PREFIX := arm-eabi-
LZMA_RAMDISK_TARGETS := recovery

TARGET_PROCESS_SDK_VERSION_OVERRIDE += \
    /vendor/bin/hw/rild=19

TARGET_PROCESS_SDK_VERSION_OVERRIDE := \
    /vendor/bin/hw/rild=22 \
    /vendor/lib/libsec-ril.so=22 \
    /vendor/lib/libsecnativefeature.so=22 \
    /system/lib/libomission_avoidance.so=22 \
    /system/lib/libfactoryutil.so=22 \
    /vendor/lib/libakm.so=22 \
    /vendor/lib/libsecril-client.so=22 \
    /vendor/lib/hw/gps.exynos4.vendor.so=22 \
    /vendor/lib/hw/gps.default.so=22 \
    /vendor/bin/glgps=22 \
    /vendor/bin/gpsd=22

WITH_DEXPREOPT := true
USE_DEX2OAT_DEBUG := false
WITH_DEXPREOPT_DEBUG_INFO := false

# Filesystem
TARGET_FS_CONFIG_GEN := device/samsung/smdk4412-common/config.fs
#BOARD_NAND_PAGE_SIZE := 4096
#BOARD_NAND_SPARE_SIZE := 128
BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 16777216
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1610612736
BOARD_VENDORIMAGE_PARTITION_SIZE := 524288000
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USES_VENDORIMAGE := true
BOARD_USERDATAIMAGE_PARTITION_SIZE := 12381585408
BOARD_FLASH_BLOCK_SIZE := 4096
BOARD_ROOT_EXTRA_FOLDERS := efs misc preload
BOARD_ROOT_EXTRA_SYMLINKS := /data/tombstones:/tombstones
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_COPY_OUT_VENDOR := vendor
BOARD_PROPERTY_OVERRIDES_SPLIT_ENABLED := true

TOP_PATH := $(realpath $(TOP))
KERNEL_TOOLCHAIN := $(TOP_PATH)/prebuilts/gcc/$(HOST_OS)-x86/arm/arm-linux-androideabi-4.9/bin
TARGET_KERNEL_CROSS_COMPILE_PREFIX := arm-linux-androidkernel-

# Hardware tunables
BOARD_HARDWARE_CLASS := hardware/samsung/lineagehw \
    device/samsung/smdk4412-common/lineagehw

# Graphics
USE_OPENGL_RENDERER := true
BOARD_USES_SKIAHWJPEG := true
TARGET_PROVIDES_LIBEGL_MALI := true
TARGET_NEEDS_NATIVE_WINDOW_FORMAT_FIX := true
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3
BOARD_EGL_NEEDS_HANDLE_VALUE := true
# Only needed by Samsung skia changes (not ported beyond 4.4)
#BOARD_GLOBAL_CFLAGS += -DSEC_HWJPEG_G2D

# FIMG Acceleration
BOARD_USES_FIMGAPI := true
BOARD_USES_SKIA_FIMGAPI := true

# Enable WEBGL in WebKit
ENABLE_WEBGL := true

# HWComposer
BOARD_USES_HWCOMPOSER := true
BOARD_USE_SYSFS_VSYNC_NOTIFICATION := true

# Camera
BOARD_CAMERA_HAVE_ISO := true
BOARD_CAMERA_MSG_MGMT := true
TARGET_HAS_LEGACY_CAMERA_HAL1 := true

# OMX
BOARD_NONBLOCK_MODE_PROCESS := true
BOARD_USE_STOREMETADATA := true
BOARD_USE_METADATABUFFERTYPE := true
BOARD_USES_MFC_FPS := true
BOARD_USE_S3D_SUPPORT := true
BOARD_USE_CSC_FIMC := false

# Include an expanded selection of fonts
EXTENDED_FONT_FOOTPRINT := true

# Logging
TARGET_USES_LOGD := false

# RIL
BOARD_MOBILEDATA_INTERFACE_NAME := "pdp0"
# RIL
BOARD_PROVIDES_LIBRIL := true
BOARD_MODEM_TYPE := xmm6262
TARGET_SPECIFIC_HEADER_PATH += device/samsung/smdk4412-common/include
BOARD_RIL_CLASS := ../../../device/samsung/i9300/ril
TARGET_NEEDS_NETD_DIRECT_CONNECT_RULE := true

# Wifi
BOARD_WLAN_DEVICE                := bcmdhd
BOARD_WLAN_DEVICE_REV            := bcm4334
WPA_SUPPLICANT_VERSION           := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER      := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_bcmdhd
BOARD_HOSTAPD_DRIVER             := NL80211
BOARD_HOSTAPD_PRIVATE_LIB        := lib_driver_cmd_bcmdhd
WIFI_DRIVER_FW_PATH_PARAM        := "/sys/module/dhd/parameters/firmware_path"
WIFI_DRIVER_FW_PATH_STA          := "/vendor/etc/wifi/bcmdhd_sta.bin"
WIFI_DRIVER_FW_PATH_AP           := "/vendor/etc/wifi/bcmdhd_apsta.bin"
WIFI_DRIVER_FW_PATH_P2P          := "/vendor/etc/wifi/bcmdhd_p2p.bin"
WIFI_BAND                        := 802_11_ABG
BOARD_HAVE_SAMSUNG_WIFI          := true
WIFI_HIDL_UNIFIED_SUPPLICANT_SERVICE_RC_ENTRY := true

# Network Routing
TARGET_NEEDS_NETD_DIRECT_CONNECT_RULE := true

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
BOARD_HAVE_SAMSUNG_BLUETOOTH := true
BOARD_CUSTOM_BT_CONFIG := device/samsung/smdk4412-common/bluetooth/vnd_smdk4x12.txt

# Vold
TARGET_USE_CUSTOM_LUN_FILE_PATH := "/sys/devices/platform/s3c-usbgadget/gadget/lun%d/file"

# Recovery
BOARD_CUSTOM_RECOVERY_KEYMAPPING := ../../device/samsung/smdk4412-common/recovery/recovery_keys.c
BOARD_CUSTOM_GRAPHICS := ../../../device/samsung/smdk4412-common/recovery/graphics.c
BOARD_USE_CUSTOM_RECOVERY_FONT := \"roboto_15x24.h\"
BOARD_UMS_LUNFILE := "/sys/class/android_usb/android0/f_mass_storage/lun0/file"
BOARD_USES_MMCUTILS := true
BOARD_HAS_LARGE_FILESYSTEM := true
BOARD_HAS_NO_MISC_PARTITION := true
BOARD_HAS_NO_SELECT_BUTTON := true

# TWRP Support - Optional
ifeq ($(WITH_TWRP),true)
-include device/samsung/smdk4412-common/twrp/twrp.mk
endif

# Charging mode
BOARD_BATTERY_DEVICE_NAME := "battery"
WITH_LINEAGE_CHARGER := false
RED_LED_PATH := /sys/class/leds/led_r/brightness
GREEN_LED_PATH := /sys/class/leds/led_g/brightness
BLUE_LED_PATH := /sys/class/leds/led_b/brightness
BACKLIGHT_PATH := /sys/class/backlight/panel/brightness

SELINUX_IGNORE_NEVERALLOWS := true

# LPM Battery Percentage
BOARD_CHARGER_SHOW_PERCENTAGE := true

# Releasetools
TARGET_RELEASETOOLS_EXTENSIONS := ./device/samsung/smdk4412-common

# inherit from the proprietary version
-include vendor/samsung/smdk4412-common-treble/BoardConfigVendor.mk

DEXPREOPT_GENERATE_APEX_IMAGE := false
DEXPREOPT_USE_APEX_IMAGE := false

# Releasetools
TARGET_RELEASETOOLS_EXTENSIONS := $(DEVICE_PATH)/releasetools

# Treble
BOARD_VNDK_VERSION := current
# BOARD_VNDK_RUNTIME_DISABLE := true
# PRODUCT_FULL_TREBLE_OVERRIDE := true