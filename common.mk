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

COMMON_PATH := device/samsung/smdk4412-common

DEVICE_PACKAGE_OVERLAYS := $(COMMON_PATH)/overlay

# Vendor properties
-include $(LOCAL_PATH)/vendor_prop.mk

PRODUCT_BUILD_PROP_OVERRIDES += BUILD_UTC_DATE=0

# Init files
PRODUCT_COPY_FILES := \
    $(LOCAL_PATH)/rootdir/init.smdk4x12.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.smdk4x12.rc \
    $(LOCAL_PATH)/rootdir/init.smdk4x12.usb.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.smdk4x12.usb.rc \
    $(COMMON_PATH)/rootdir/init.trace.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.trace.rc \
    $(LOCAL_PATH)/rootdir/ueventd.smdk4x12.rc:$(TARGET_COPY_OUT_VENDOR)/ueventd.rc \
    $(COMMON_PATH)/rootdir/ueventd.smdk4x12.rc:root/ueventd.smdk4x12.rc \
    $(COMMON_PATH)/rootdir/ueventd.smdk4x12.rc:recovery/root/ueventd.smdk4x12.rc

# init.d
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/tweaks.rc:vendor/etc/init/tweaks.rc \
    $(LOCAL_PATH)/configs/MagiskHide:vendor/etc/init/MagiskHide.rc


# linkerconfig
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/prebuilt/linkerconfig:vendor/bin/linkerconfig \
    $(COMMON_PATH)/prebuilt/libc.so:vendor/lib/bionic/libc.so \
	$(LOCAL_PATH)/configs/fix_gsi.sh:vendor/fixgsi.sh

# Disable traced and iorapd
PRODUCT_PROPERTY_OVERRIDES += \
	ro.iorapd.enable=false \
	persist.traced.enable=0

# RIL subscription workaround
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/restart_rild.sh:vendor/bin/restart_rild.sh \
    $(LOCAL_PATH)/configs/rild_restart.rc:vendor/etc/init/rild_restart.rc

# main route table fix
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/main_route_table.sh:vendor/bin/main_route_table.sh \
    $(LOCAL_PATH)/configs/main_route_table.rc:vendor/etc/init/main_route_table.rc

ifneq ($(filter TARGET_PRODUCT, lineage_n8020 lineage_n5120),)
# Gps
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/gps.conf:vendor/etc/gps.conf \
    $(COMMON_PATH)/configs/gps_debug.conf:$(TARGET_COPY_OUT_SYSTEM)/etc/gps_debug.conf
endif

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service.basic \
    com.android.future.usb.accessory

# Use legacy ADB USB support
PRODUCT_PROPERTY_OVERRIDES += \
    ro.adb.nonblocking_ffs=false

# Trust HAL
PRODUCT_PACKAGES += \
    vendor.lineage.trust@1.0-service

# Battery
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.small_battery=true

# SamsungPowerHAL
PRODUCT_PACKAGES += \
    android.hardware.power@1.0-service.exynos4412

# Light
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-service.samsung

# LiveDisplay
PRODUCT_PACKAGES += \
	vendor.lineage.livedisplay@2.0-service.samsung-exynos

# SamsungDoze
PRODUCT_PACKAGES += \
    SamsungDoze

# Audio
PRODUCT_PACKAGES += \
    android.hardware.audio@6.0-impl \
    android.hardware.audio.effect@6.0-impl \
    audio.a2dp.default \
    audio.primary.smdk4x12 \
    audio.r_submix.default \
    audio.usb.default \
    tinymix

PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    $(COMMON_PATH)/audio/silence.wav:vendor/etc/sound/silence.wav \
    $(COMMON_PATH)/configs/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(COMMON_PATH)/configs/a2dp_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    $(COMMON_PATH)/configs/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml \
    $(COMMON_PATH)/configs/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml

PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml

# Bluetooth
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-impl \
    libbt-vendor

# Graphics
PRODUCT_PACKAGES += \
    AdvancedDisplay \
    android.hardware.graphics.allocator@2.0-impl-exynos4 \
    android.hardware.graphics.mapper@2.0-impl-exynos4 \
    android.hardware.graphics.composer@2.1-impl \
    android.hardware.graphics.composer@2.1-service \
    hwcomposer.exynos4 \
    gralloc.exynos4 \
    libUMP \
    libMali \
    libEGL_mali \
    libfimc \
    libfimg \
    libhwjpeg \
    libsecion \
    libsync

# Camera
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.5-impl \
    android.hardware.camera.provider@2.5-service \
    camera.device@1.0-impl \
    gCam

PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/80cfw:vendor/etc/init.d/80cfw

# Seccomp Policy
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/seccomp_policy/mediacodec.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediacodec.policy

# Sensors
PRODUCT_PACKAGES += \
    android.hardware.sensors@1.0-impl

# Legacy GPS
PRODUCT_PACKAGES += \
    android.hardware.gnss@1.0-impl \
    android.hardware.gnss@1.0-service.exynos4

# Vibrator
PRODUCT_PACKAGES += \
    android.hardware.vibrator@1.0-impl

# Keymaster
PRODUCT_PACKAGES += \
    android.hardware.keymaster@3.0-impl \
    android.hardware.authsecret@1.0-service \
    android.hardware.gatekeeper@1.0-service.software

# Misc stuff
PRODUCT_PACKAGES += \
    libnetcmdiface \
    libstagefright-shim \
    libC

# Health
PRODUCT_PACKAGES += \
    android.hardware.health@2.0-impl \
    android.hardware.health@2.0-service

# DRM
PRODUCT_PACKAGES += \
    android.hardware.drm@1.0-impl \
    android.hardware.drm@1.0-service

# MFC API
PRODUCT_PACKAGES += \
    libsecmfcdecapi \
    libsecmfcencapi

# OMX
PRODUCT_PACKAGES += \
    libhwconverter \
    libstagefrighthw \
    libSEC_OMX_Resourcemanager \
    libSEC_OMX_Core \
    libOMX.SEC.AVC.Decoder \
    libOMX.SEC.M4V.Decoder \
    libOMX.SEC.WMV.Decoder \
    libOMX.SEC.AVC.Encoder \
    libOMX.SEC.M4V.Encoder
#   libOMX.SEC.VP8.Decoder

# VNDK
PRODUCT_PACKAGES += \
    vndk_package \
    libstdc++.vendor

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/mediaserver.rc:system/etc/init/mediaserver.rc


PRODUCT_PROPERTY_OVERRIDES += \
    media.settings.xml=/vendor/etc/media_profiles.xml \
    debug.stagefright.ccodec=0 \
    media.stagefright.legacyencoder=true \
    media.stagefright.less-secure=true

PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/media_profiles.xml:vendor/etc/media_profiles.xml \
    $(COMMON_PATH)/configs/media_profiles.xml:vendor/etc/media_profiles_V1_0.xml \
    $(COMMON_PATH)/configs/media_codecs.xml:vendor/etc/media_codecs.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:vendor/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_telephony.xml:vendor/etc/media_codecs_google_telephony.xml \
    $(COMMON_PATH)/configs/media_codecs_google_video_le.xml:vendor/etc/media_codecs_google_video_le.xml

# Memory Optimizations
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.qti.am.reschedule_service=true \
    ro.vendor.qti.sys.fw.use_trim_settings=true \
    ro.vendor.qti.sys.fw.trim_empty_percent=50 \
    ro.vendor.qti.sys.fw.trim_cache_percent=100 \
    ro.vendor.qti.sys.fw.empty_app_percent=25

# Filesystem management tools
PRODUCT_PACKAGES += \
    make_ext4fs \
    setup_fs

# Live Wallpapers
PRODUCT_PACKAGES += \
    Galaxy4 \
    HoloSpiralWallpaper \
    LiveWallpapers \
    LiveWallpapersPicker \
    MagicSmokeWallpapers \
    NoiseField \
    PhaseBeam \
    VisualizationWallpapers \
    librs_jni

# Wifi
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0-service \
    android.hardware.wifi.supplicant@1.0 \
    macloader \
    wificond \
    libwpa_client \
    hostapd \
    dhcpcd.conf \
    wpa_supplicant \
    wpa_supplicant.conf

PRODUCT_PROPERTY_OVERRIDES += \
    wifi.interface=wlan0 \
    wifi.supplicant_scan_interval=30 \
    net.tethering.noprovisioning=true

PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/wpa_supplicant_overlay.conf:vendor/etc/wifi/wpa_supplicant_overlay.conf \
    $(COMMON_PATH)/configs/p2p_supplicant_overlay.conf:vendor/etc/wifi/p2p_supplicant_overlay.conf

# These are the hardware-specific features
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:vendor/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.camera.autofocus.xml:vendor/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:vendor/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:vendor/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.xml:vendor/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:vendor/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.location.xml:vendor/etc/permissions/android.hardware.location.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:vendor/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:vendor/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:vendor/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:vendor/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:vendor/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:vendor/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.xml:vendor/etc/permissions/android.hardware.touchscreen.multitouch.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.xml:vendor/etc/permissions/android.hardware.touchscreen.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:vendor/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:vendor/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:vendor/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:vendor/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:vendor/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/android.software.sip.xml:vendor/etc/permissions/android.software.sip.xml

# Feature live wallpaper
PRODUCT_COPY_FILES += \
    packages/wallpapers/LivePicker/android.software.live_wallpaper.xml:vendor/etc/permissions/android.software.live_wallpaper.xml

PRODUCT_TAGS += dalvik.gc.type-precise

# Keylayouts
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/excluded-input-devices.xml:system/etc/excluded-input-devices.xml

# Stylus gestures
PRODUCT_PACKAGES += \
    com.lineageos.keyhandler \
    vendor.lineage.touch@1.0-service.samsung

# Graphics
PRODUCT_PROPERTY_OVERRIDES += \
    ro.zygote.disable_gl_preload=1 \
    ro.opengles.version=131072 \
    ro.bq.gpu_to_cpu_unsupported=1 \
    debug.hwui.render_dirty_regions=false

# Include exynos4 platform specific parts
TARGET_HAL_PATH := hardware/samsung/exynos4/hal
TARGET_OMX_PATH := hardware/samsung/exynos/multimedia/openmax
$(call inherit-product, hardware/samsung/exynos4x12.mk)

# Shipping API level
$(call inherit-product, $(SRC_TARGET_DIR)/product/product_launched_with_k.mk)

# Include non-opensource parts
$(call inherit-product, vendor/samsung/smdk4412-common-treble/smdk4412-common-vendor.mk)

# Include Lineage sepolicy for Exynos
$(call inherit-product, device/lineage/sepolicy/exynos/sepolicy.mk)

# Art
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
    dalvik.vm.dex2oat-threads=1 \
    dalvik.vm.image-dex2oat-threads=1

# Build with specific settings for Galaxys2-common
$(call inherit-product, $(LOCAL_PATH)/go_galaxys2-common.mk)

# Apply Dalvik config for 1G phone
$(call inherit-product, frameworks/native/build/phone-xhdpi-1024-dalvik-heap.mk)

# Include debugging props
$(call inherit-product, device/samsung/smdk4412-common/system_prop_debug.mk)
