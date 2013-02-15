# config.mk
#
# Product-specific compile-time definitions.
#
TARGET_BOARD_PLATFORM := exynos4
TARGET_SOC := exynos4x12

# CPU options
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_SMP := true
ARCH_ARM_HAVE_TLS_REGISTER := true

TARGET_NO_BOOTLOADER := true
#TARGET_NO_KERNEL := true
#TARGET_NO_RECOVERY := true
TARGET_NO_RADIOIMAGE := true
TARGET_PROVIDES_INIT_TARGET_RC := true
TARGET_BOOTLOADER_BOARD_NAME := odroidu2

BOARD_USES_GENERIC_AUDIO := false
BOARD_USES_I2S_AUDIO := true
BOARD_USES_PCM_AUDIO := false
BOARD_USES_SPDIF_AUDIO := false

# ULP Audio
USE_ULP_AUDIO := false

# ALP Audio
BOARD_USE_ALP_AUDIO := false

# SEC Camera
USE_SEC_CAMERA := false
CAMERA_USE_DIGITALZOOM := true

# Enable JIT
WITH_JIT := true

TARGET_USERIMAGES_USE_EXT4 := true
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 524288000
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1073741824
BOARD_FLASH_BLOCK_SIZE := 4096

BOARD_USES_EMMC := true

BOARD_USES_HGL := true
BOARD_NO_32BPP := true

BOARD_USES_HDMI_SUBTITLES := true
BOARD_USES_HDMI := true
BOARD_HDMI_STD := STD_720P
BOARD_HDMI_DDC_CH := DDC_CH_I2C_2
BOARD_USES_FIMGAPI := true

#
# if BOARD_USES_HDMI_EDID == false && BOARD_USES_HDMI_JUMPER == true
# JP1 short = HDMI 1080P, JP1 open = HDMI 720P
#
BOARD_USES_HDMI_EDID := true
BOARD_USES_HDMI_JUMPER := false

BOARD_USE_SAMSUNG_COLORFORMAT := true
BOARD_FIX_NATIVE_COLOR_FORMAT := true
BOARD_NONBLOCK_MODE_PROCESS := true
BOARD_USE_STOREMETADATA := true
BOARD_USE_METADATABUFFERTYPE := true
BOARD_USES_MFC_FPS := false
BOARD_USE_S3D_SUPPORT := true
BOARD_USE_DRM := true
BOARD_USE_CSC_FIMC := false

BOARD_USES_HIGH_RESOLUTION_LCD := true

# Enable V4L2 & ION
BOARD_USE_V4L2 := false
BOARD_USE_V4L2_ION := false

SCREEN_WIDTH := 1280
SCREEN_HEIGHT := 800
DEFAULT_FB_NUM := 0

#
# Wifi related defines
#
# ralink module = rt5370sta, realtek = rtl8191su
#
BOARD_WLAN_DEVICE	:= rtl8191su

ifeq ($(BOARD_WLAN_DEVICE), rt5370sta)
    WPA_SUPPLICANT_VERSION              := VER_0_8_X
    BOARD_WPA_SUPPLICANT_DRIVER         := WEXT
    BOARD_WPA_SUPPLICANT_PRIVATE_LIB    := lib_driver_cmd
    WIFI_DRIVER_MODULE_NAME		        := "rt5370sta"
    WIFI_DRIVER_MODULE_PATH             := "/system/lib/modules/rt5370sta.ko"
endif    

ifeq ($(BOARD_WLAN_DEVICE), rtl8191su)
    WPA_SUPPLICANT_VERSION              := VER_0_8_X
    BOARD_WPA_SUPPLICANT_DRIVER         := WEXT
    BOARD_WPA_SUPPLICANT_PRIVATE_LIB    := lib_driver_cmd_rtl

#    WPA_SUPPLICANT_VERSION              := VER_0_8_X
#    BOARD_WPA_SUPPLICANT_DRIVER         := NL80211
#    BOARD_WPA_SUPPLICANT_PRIVATE_LIB    := lib_driver_cmd_rtl
#    BOARD_HOSTAPD_DRIVER                := NL80211
#    BOARD_HOSTAPD_PRIVATE_LIB           := lib_driver_cmd_rtl

    WIFI_DRIVER_MODULE_NAME             := "rtl8191su"
    WIFI_DRIVER_MODULE_PATH             := "/system/lib/modules/rtl8191su.ko"

    # Realtek driver has FW embedded inside, and will automatically load FW
    # at NIC initialization process. So there is no need to set these 
    # 5 variables.
    WIFI_DRIVER_MODULE_ARG           := ""
    WIFI_FIRMWARE_LOADER             := ""
    WIFI_DRIVER_FW_PATH_STA          := ""
    WIFI_DRIVER_FW_PATH_AP           := ""
    WIFI_DRIVER_FW_PATH_P2P          := ""
    WIFI_DRIVER_FW_PATH_PARAM        := ""
endif
BOARD_HAVE_BLUETOOTH := true
#BOARD_HAVE_BLUETOOTH_USB := true

USE_OPENGL_RENDERER := true
BOARD_CAMERA	:= odroidx

BOARD_HAVE_ODROID_GPS := true
BOARD_SUPPORT_EXTERNAL_GPS := true

# Try to build the kernel
TARGET_KERNEL_CONFIG := odroidu2_android_emmc_defconfig
TARGET_KERNEL_SOURCE := kernel/hardkernel/4412-common

COMMON_GLOBAL_CFLAGS += -DEXYNOS4_ENHANCEMENTS

BOARD_CUSTOM_BOOTIMG_MK := device/hardkernel/odroidu2/uboot-bootimg.mk

BOARD_USES_SAMSUNG_HDMI := true

TARGET_SPECIFIC_HEADER_PATH := device/hardkernel/odroidu2/include

BOARD_EGL_NEEDS_LEGACY_FB := true

COMMON_GLOBAL_CFLAGS += -Idevice/hardkernel/samsung/$(TARGET_BOARD_PLATFORM)/libhdmi/libhdmiservice
COMMON_GLOBAL_CFLAGS += -Idevice/hardkernel/samsung/$(TARGET_BOARD_PLATFORM)/include

TARGET_RECOVERY_PRE_COMMAND := "/system/bin/setup-recovery"
BOARD_CUSTOM_GRAPHICS := ../../../device/hardkernel/odroidu2/recovery/graphics.c

TARGET_USERIMAGES_SPARSE_EXT_DISABLED := true
TARGET_RELEASETOOL_OTA_FROM_TARGET_SCRIPT := device/hardkernel/odroidu2/releasetools/odroid_ota_from_target_files
