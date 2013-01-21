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
#include <cutils/native_handle.h>
#include <hal_public.h>
#include <ui/GraphicBufferMapper.h>
#include <gui/ISurfaceTexture.h>
#define MAX_VIDEONODE      5
#define MIN_VIDEONODE      4
#define MIN_WIDTH           320
#define MIN_HEIGHT          240
#define CAM_SIZE            "320x240"
#define PIXEL_FORMAT        V4L2_PIX_FMT_YUYV
#define CAMHAL_GRALLOC_USAGE GRALLOC_USAGE_HW_TEXTURE | \
                             GRALLOC_USAGE_HW_RENDER | \
                             GRALLOC_USAGE_SW_READ_RARELY | \
                             GRALLOC_USAGE_SW_WRITE_NEVER

extern "C" {
    void yuyv422_to_yuv420sp(unsigned char*,unsigned char*,int,int);
    void convertYUYVtoRGB565(unsigned char *buf, unsigned char *rgb, int width, int height);
}

namespace android {


const char supportedFpsRanges [] = "(8000,8000),(8000,10000),(10000,10000),(8000,15000),(15000,15000),(8000,20000),(20000,20000),(24000,24000),(25000,25000),(8000,30000),(30000,30000)";

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
    mNativeWindow=NULL;
}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;

    p.setPreviewSize(MIN_WIDTH, MIN_HEIGHT);
    p.setPreviewFrameRate(30);
    p.setPreviewFormat("yuv422sp");
    p.set(p.KEY_SUPPORTED_PREVIEW_SIZES, CAM_SIZE);
    p.set(p.KEY_SUPPORTED_PREVIEW_SIZES, "640x480");
    p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(CameraParameters::KEY_FOCUS_MODE,0);
    p.setPictureSize(MIN_WIDTH, MIN_HEIGHT);
    p.setPictureFormat("jpeg");
    p.set(p.KEY_SUPPORTED_PICTURE_SIZES, CAM_SIZE);
    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "fixed");
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0");
    p.set(CameraParameters::KEY_VIDEO_STABILIZATION_SUPPORTED, "false");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "8,10,12,15,20,24,25,30");

    if (setParameters(p) != NO_ERROR) {
        ALOGE("Failed to set default parameters?!");
    }
}

CameraHardware::~CameraHardware()
{
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

void CameraHardware::setCallbacks(camera_notify_callback notify_cb,
                                  camera_data_callback data_cb,
                                  camera_data_timestamp_callback data_cb_timestamp,
                                  camera_request_memory get_memory,
                                  void *arg)
{
    Mutex::Autolock lock(mLock);
    mNotifyFn = notify_cb;
    mDataFn = data_cb;
    mRequestMemory = get_memory;
    mTimestampFn = data_cb_timestamp;
    mUser = arg;
}

int CameraHardware::setPreviewWindow( preview_stream_ops_t *window)
{
    int err;
    Mutex::Autolock lock(mLock);
        if(mNativeWindow)
            mNativeWindow=NULL;
    if(window==NULL)
    {
        ALOGW("Window is Null");
        return 0;
    }
    int width, height;
    mParameters.getPreviewSize(&width, &height);
    mNativeWindow=window;
    mNativeWindow->set_usage(mNativeWindow,CAMHAL_GRALLOC_USAGE);
    mNativeWindow->set_buffers_geometry(
                mNativeWindow,
                width,
                height,
                HAL_PIXEL_FORMAT_RGB_565);
    err = mNativeWindow->set_buffer_count(mNativeWindow, 3);
    if (err != 0) {
        ALOGE("native_window_set_buffer_count failed: %s (%d)", strerror(-err), -err);

        if ( ENODEV == err ) {
            ALOGE("Preview surface abandoned!");
            mNativeWindow = NULL;
        }
    }

    return 0;
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
    int width, height;
    int err;
    IMG_native_handle_t** hndl2hndl;
    IMG_native_handle_t* handle;
    int stride;
    mParameters.getPreviewSize(&width, &height);
    int framesize= width * height * 1.5 ; //yuv420sp

   if (!previewStopped) {
    mLock.lock();
    if (mNativeWindow != NULL)
    {
    if ((err = mNativeWindow->dequeue_buffer(mNativeWindow,(buffer_handle_t**) &hndl2hndl,&stride)) != 0) {
        ALOGW("Surface::dequeueBuffer returned error %d", err);
        return -1;
    }
    mNativeWindow->lock_buffer(mNativeWindow, (buffer_handle_t*) hndl2hndl);
    GraphicBufferMapper &mapper = GraphicBufferMapper::get();

    Rect bounds(width, height);
    void *tempbuf;
    void *dst;
    if(0 == mapper.lock((buffer_handle_t)*hndl2hndl,CAMHAL_GRALLOC_USAGE, bounds, &dst)); 
    {
        // Get preview frame
        tempbuf=camera.GrabPreviewFrame();
        convertYUYVtoRGB565((unsigned char *)tempbuf,(unsigned char *)dst, width, height);
        mapper.unlock((buffer_handle_t)*hndl2hndl);
        mNativeWindow->enqueue_buffer(mNativeWindow,(buffer_handle_t*) hndl2hndl);
        if ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) ||
                (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
            camera_memory_t* picture = mRequestMemory(-1, framesize, 1, NULL);
            yuyv422_to_yuv420sp((unsigned char *)tempbuf,(unsigned char *) picture->data, width, height);
            if ((mMsgEnabled & CAMERA_MSG_VIDEO_FRAME ) && mRecordRunning ) {
                nsecs_t timeStamp = systemTime(SYSTEM_TIME_MONOTONIC);
                //mTimestampFn(timeStamp, CAMERA_MSG_VIDEO_FRAME,mRecordBuffer, mUser);
            }
            mDataFn(CAMERA_MSG_PREVIEW_FRAME,picture,0,NULL,mUser);
	    picture->release(picture);
	}
        camera.ReleasePreviewFrame();
    }
    }
    mLock.unlock();
    }

    return NO_ERROR;
}

status_t CameraHardware::startPreview()
{
    int ret;
    int width, height;
    int i;
    IMG_native_handle_t** hndl2hndl;
    IMG_native_handle_t* handle;
    int stride;
    char devnode[15];
    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        //already running
        return INVALID_OPERATION;
    }
#if 1
    ALOGI("startPreview: in startpreview \n");
    mParameters.getPreviewSize(&width, &height);
    for( i=MAX_VIDEONODE; i>=MIN_VIDEONODE; i--) {
        sprintf(devnode,"/dev/video%d",i);
        ALOGI("trying the node %s width=%d height=%d \n",devnode,width,height);
        ret = camera.Open(devnode, width, height, PIXEL_FORMAT);
        if( ret >= 0)
            break;
        }

    if( ret < 0)
        return -1;

    mPreviewFrameSize = width * height * 2;

    mHeap = new MemoryHeapBase(mPreviewFrameSize);
    mBuffer = new MemoryBase(mHeap, 0, mPreviewFrameSize);

    ret = camera.Init();
    if (ret != 0) {  
        ALOGI("startPreview: Camera.Init failed\n");
        camera.Close();
        return ret;
    }

    ret = camera.StartStreaming();
    if (ret != 0) {  
        ALOGI("startPreview: Camera.StartStreaming failed\n");
        camera.Uninit();
        camera.Close();
        return ret;
    }

    previewStopped = false;
    mPreviewThread = new PreviewThread(this);

#endif
    return NO_ERROR;
}

void CameraHardware::stopPreview()
{
    sp<PreviewThread> previewThread;

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewStopped = true;
    }

    {
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    if (mPreviewThread != 0) {
        camera.Uninit();
        camera.StopStreaming();
        camera.Close();
    }

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();
}

bool CameraHardware::previewEnabled()
{
    Mutex::Autolock lock(mLock);
    return ((mPreviewThread != 0) );
}

status_t CameraHardware::startRecording()
{
    Mutex::Autolock lock(mLock);

    mRecordHeap = new MemoryHeapBase(mPreviewFrameSize*3/4);
    mRecordBuffer = new MemoryBase(mRecordHeap, 0, mPreviewFrameSize*3/4);
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

void CameraHardware::releaseRecordingFrame(const void *opaque)
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
    int i;
    char devnode[15];
    camera_memory_t* picture = NULL;


   if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        mNotifyFn(CAMERA_MSG_SHUTTER, 0, 0, mUser);

    mParameters.getPictureSize(&w, &h);
    ALOGD("Picture Size: Width = %d \t Height = %d", w, h);

    int width, height;
    mParameters.getPictureSize(&width, &height);
    mParameters.getPreviewSize(&width, &height);

    for(i=MAX_VIDEONODE; i>=MIN_VIDEONODE; i--) {
        sprintf(devnode,"/dev/video%d",i);
        ALOGI("trying the node %s \n",devnode);
        ret = camera.Open(devnode, width, height, PIXEL_FORMAT);
        if( ret >= 0)
            break;
    }

    if( ret < 0)
        return -1;

    camera.Init();
    camera.StartStreaming();
    //TODO xxx : Optimize the memory capture call. Too many memcpy
    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        ALOGD ("mJpegPictureCallback");
        picture = camera.GrabJpegFrame(mRequestMemory);
        mDataFn(CAMERA_MSG_COMPRESSED_IMAGE,picture,0,NULL ,mUser);
    }

    camera.Uninit();
    camera.StopStreaming();
    camera.Close();

    return NO_ERROR;
}

status_t CameraHardware::takePicture()
{
        ALOGD ("takepicture");
    stopPreview();

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

    if (strcmp(params.getPictureFormat(), "jpeg") != 0) {
        ALOGE("Only jpeg still pictures are supported");
        return -1;
    }

    int w, h;
    int framerate;

    mParameters = params;
    params.getPictureSize(&w, &h);
    mParameters.setPictureSize(w,h);
    params.getPreviewSize(&w, &h);
    mParameters.setPreviewSize(w,h);
    framerate = params.getPreviewFrameRate();
    ALOGD("PREVIEW SIZE: w=%d h=%d framerate=%d", w, h, framerate);
    mParameters = params;
    mParameters.setPreviewSize(w,h);
    mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, supportedFpsRanges);
    mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, "320x240,352x288,640x480,720x480,720x576,848x480");

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

}; // namespace android
