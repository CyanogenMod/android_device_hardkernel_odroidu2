/*
**
** Copyright 2009, The Android-x86 Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
** Author: Niels Keeman <nielskeeman@gmail.com>
**
*/

#define LOG_TAG "CameraHardware"
#include <utils/Log.h>

#include "CameraHardware.h"
#include <fcntl.h>
#include <sys/mman.h>

#define VIDEO_DEVICE        "/dev/video0"
#define MIN_WIDTH           640
#define MIN_HEIGHT          480
#define CAM_SIZE            "640x480"
#define PIXEL_FORMAT        V4L2_PIX_FMT_YUYV

namespace android {

wp<CameraHardwareInterface> CameraHardware::singleton;

CameraHardware::CameraHardware(int cameraId)
                  : mCameraId(cameraId),
                    mParameters(),
                    mHeap(0),
                    mPreviewHeap(0),
                    mRecordHeap(0),
                    mRawHeap(0),
                    mPreviewFrameSize(0),
                    mCurrentPreviewFrame(0),
                    mRecordRunning(false),
                    previewStopped(true),
                    nQueued(0),
                    nDequeued(0),
                    mNotifyFn(NULL),
                    mDataFn(NULL),
                    mTimestampFn(NULL),
                    mUser(NULL),
                    mMsgEnabled(0)
{
    initDefaultParameters();
}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;

    p.setPreviewSize(MIN_WIDTH, MIN_HEIGHT);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat("yuv422sp");
    p.set(p.KEY_SUPPORTED_PREVIEW_SIZES, CAM_SIZE);

    p.setPictureSize(MIN_WIDTH, MIN_HEIGHT);
    p.setPictureFormat("jpeg");
    p.set(p.KEY_SUPPORTED_PICTURE_SIZES, CAM_SIZE);

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }
}

CameraHardware::~CameraHardware()
{
    singleton.clear();
}

sp<IMemoryHeap> CameraHardware::getPreviewHeap() const
{
    return mHeap;
}

sp<IMemoryHeap> CameraHardware::getRawHeap() const
{
    return mRawHeap;
}

// ---------------------------------------------------------------------------

void CameraHardware::setCallbacks(notify_callback notify_cb,
                                  data_callback data_cb,
                                  data_callback_timestamp data_cb_timestamp,
                                  void *arg)
{
    Mutex::Autolock lock(mLock);
    mNotifyFn = notify_cb;
    mDataFn = data_cb;
    mTimestampFn = data_cb_timestamp;
    mUser = arg;
}

void CameraHardware::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void CameraHardware::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool CameraHardware::msgTypeEnabled(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
}


//-------------------------------------------------------------
int CameraHardware::previewThread()
{
    if (!previewStopped) {
        // Get preview frame
        camera.GrabPreviewFrame(mHeap->getBase());
        if ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) ||
		(mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
            if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
			camera.GrabRecordFrame(mRecordHeap->getBase());
			nsecs_t timeStamp = systemTime(SYSTEM_TIME_MONOTONIC);
			mTimestampFn(timeStamp, CAMERA_MSG_VIDEO_FRAME,mRecordBuffer, mUser);
            }
            mDataFn(CAMERA_MSG_PREVIEW_FRAME,mBuffer, mUser);
	}
    }

    return NO_ERROR;
}

status_t CameraHardware::startPreview()
{
    int ret;
    int width, height;

    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        //already running
        return INVALID_OPERATION;
    }

        LOGI("startPreview: in startpreview \n");
    camera.Open(VIDEO_DEVICE, MIN_WIDTH, MIN_HEIGHT, PIXEL_FORMAT);

    mPreviewFrameSize = MIN_WIDTH * MIN_HEIGHT * 2;

    mHeap = new MemoryHeapBase(mPreviewFrameSize);
    mBuffer = new MemoryBase(mHeap, 0, mPreviewFrameSize);

    ret = camera.Init();
    if (ret != 0) {  
        LOGI("startPreview: Camera.Init failed\n");
        camera.Close();
        return ret;
    }

    ret = camera.StartStreaming();
    if (ret != 0) {  
        LOGI("startPreview: Camera.StartStreaming failed\n");
        camera.Uninit();
        camera.Close();
        return ret;
    }

    previewStopped = false;
    mPreviewThread = new PreviewThread(this);

    return NO_ERROR;
}

void CameraHardware::stopPreview()
{
    sp<PreviewThread> previewThread;

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewStopped = true;
    }

    if (mPreviewThread != 0) {
        camera.Uninit();
        camera.StopStreaming();
        camera.Close();
    }

    {
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();
}

bool CameraHardware::previewEnabled()
{
    return mPreviewThread != 0;
}

status_t CameraHardware::startRecording()
{
    Mutex::Autolock lock(mLock);

    mRecordHeap = new MemoryHeapBase(mPreviewFrameSize);
    mRecordBuffer = new MemoryBase(mRecordHeap, 0, mPreviewFrameSize);
    mRecordRunning = true;

    return NO_ERROR;
}

void CameraHardware::stopRecording()
{
    mRecordRunning = false;
}

bool CameraHardware::recordingEnabled()
{
    return mRecordRunning;
}

void CameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
}

// ---------------------------------------------------------------------------

int CameraHardware::beginAutoFocusThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->autoFocusThread();
}

int CameraHardware::autoFocusThread()
{
    if (mMsgEnabled & CAMERA_MSG_FOCUS)
        mNotifyFn(CAMERA_MSG_FOCUS, true, 0, mUser);
    return NO_ERROR;
}

status_t CameraHardware::autoFocus()
{
    Mutex::Autolock lock(mLock);
    if (createThread(beginAutoFocusThread, this) == false)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

status_t CameraHardware::cancelAutoFocus()
{
    return NO_ERROR;
}

/*static*/ int CameraHardware::beginPictureThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->pictureThread();
}

int CameraHardware::pictureThread()
{
    unsigned char *frame;
    int bufferSize;
    int w,h;
    int ret;
    struct v4l2_buffer buffer;
    struct v4l2_format format;
    struct v4l2_buffer cfilledbuffer;
    struct v4l2_requestbuffers creqbuf;
    struct v4l2_capability cap;


   if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        mNotifyFn(CAMERA_MSG_SHUTTER, 0, 0, mUser);

    mParameters.getPictureSize(&w, &h);
    LOGD("Picture Size: Width = %d \t Height = %d", w, h);

    int width, height;
    mParameters.getPictureSize(&width, &height);
    camera.Open(VIDEO_DEVICE, MIN_WIDTH, MIN_HEIGHT, PIXEL_FORMAT);
    camera.Init();
    camera.StartStreaming();

    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        LOGD ("mJpegPictureCallback");
        mDataFn(CAMERA_MSG_COMPRESSED_IMAGE, camera.GrabJpegFrame(),mUser);
    }

    camera.Uninit();
    camera.StopStreaming();
    camera.Close();

    return NO_ERROR;
}

status_t CameraHardware::takePicture()
{
        LOGD ("takepicture");
    stopPreview();
    //if (createThread(beginPictureThread, this) == false)
    //    return -1;

    pictureThread();

    return NO_ERROR;
}

status_t CameraHardware::cancelPicture()
{

    return NO_ERROR;
}

status_t CameraHardware::dump(int fd, const Vector<String16>& args) const
{
    return NO_ERROR;
}

status_t CameraHardware::setParameters(const CameraParameters& params)
{
    Mutex::Autolock lock(mLock);

    if (strcmp(params.getPreviewFormat(), "yuv422sp") != 0) {
        LOGE("Only yuv422sp preview is supported");
        return -1;
    }

    if (strcmp(params.getPictureFormat(), "jpeg") != 0) {
        LOGE("Only jpeg still pictures are supported");
        return -1;
    }

    int w, h;
    int framerate;

    params.getPreviewSize(&w, &h);
    framerate = params.getPreviewFrameRate();
    LOGD("PREVIEW SIZE: w=%d h=%d framerate=%d", w, h, framerate);

    params.getPictureSize(&w, &h);

    mParameters = params;

    // Set to fixed sizes
    mParameters.setPreviewSize(MIN_WIDTH,MIN_HEIGHT);

    return NO_ERROR;
}

status_t CameraHardware::sendCommand(int32_t command, int32_t arg1, int32_t arg2)
{
    return BAD_VALUE;
}

CameraParameters CameraHardware::getParameters() const
{
    CameraParameters params;

    Mutex::Autolock lock(mLock);
    params = mParameters;

    return params;
}

void CameraHardware::release()
{
    close(camera_device);
}

sp<CameraHardwareInterface> CameraHardware::createInstance(int cameraId)
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardware(cameraId));
    singleton = hardware;
    return hardware;
}

static CameraInfo sCameraInfo[] = {
	{
		facing: CAMERA_FACING_BACK,
		orientation: 0
	},
/*
	{
		facing: CAMERA_FACING_FRONT,
		orientation: 0
	}
*/
};

extern "C" int HAL_getNumberOfCameras()
{
	return sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo* cameraInfo)
{
LOGD("HAL_getCameraInfo: %d", cameraId);
	memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(CameraInfo));
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
LOGD("HAL_openCameraHardware: %d", cameraId);
	return CameraHardware::createInstance(cameraId);
}

}; // namespace android
