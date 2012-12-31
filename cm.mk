# Boot animation
TARGET_SCREEN_HEIGHT := 720
TARGET_SCREEN_WIDTH := 1280

# Inherit some common CM stuff.
$(call inherit-product, vendor/cm/config/common_full_tablet_wifionly.mk)

# Inherit device configuration
$(call inherit-product, device/hardkernel/odroidu2/odroidu2.mk)

## Device identifier. This must come after all inclusions
PRODUCT_DEVICE := odroidu2
PRODUCT_NAME := cm_odroidu2
PRODUCT_BRAND := Hardkernel
PRODUCT_MODEL := ODROID-U2
PRODUCT_MANUFACTURER := Hardkernel
