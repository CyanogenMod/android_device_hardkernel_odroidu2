ifneq ($(USE_CAMERA_STUB),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES :=       \
    CameraHardware.cpp   \
    V4L2Camera.cpp       \
    convert.S

LOCAL_C_INCLUDES += external/jpeg

LOCAL_MODULE := libcamera
LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := \
    libui                 \
    libjpeg               \
    libutils              \
    libbinder             \
    libcutils             \
    libcamera_client

include $(BUILD_SHARED_LIBRARY)

endif
