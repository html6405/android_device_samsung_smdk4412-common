# The build variant supplies ro.secure, ro.adb.secure and ro.debuggable.
# USB defaults are platform properties; vendor_init cannot set them.
PRODUCT_SYSTEM_PROPERTIES += \
    persist.sys.usb.config=adb
