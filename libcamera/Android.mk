# omap4 has its own native camera implementation, see
# hardware/ti/omap4xxx
ifneq ($(TARGET_BOARD_PLATFORM),omap4)
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_SRC_FILES:= \
	CameraHal_Module.cpp \
        V4L2Camera.cpp \
        CameraHardware.cpp \
        convert.S \
        rgbconvert.c

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/inc/ \
    hardware/ti/omap4xxx/hwc \
    frameworks/base/include/ui \
    frameworks/base/include/utils \
    hardware/ti/omap4xxx/domx/omx_core/inc \
    hardware/ti/omap4xxx/domx/mm_osal/inc \
    frameworks/base/include/media/stagefright \
    frameworks/base/include/media/stagefright/openmax \
    external/jpeg \
    external/jhead

LOCAL_SHARED_LIBRARIES:= \
    libui \
    libbinder \
    libutils \
    libcutils \
    libcamera_client \
    libcameraservice \
    libgui \
    libjpeg \
    libexif

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE:= camera.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)
endif
