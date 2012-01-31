
#include <utils/threads.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <utils/Log.h>
#include <utils/threads.h>
#include <linux/videodev2.h>
#include "binder/MemoryBase.h"
#include "binder/MemoryHeapBase.h"
#include <utils/threads.h>
#include <camera/CameraParameters.h>
#include <hardware/camera.h>
#include <sys/ioctl.h>
#include <utils/threads.h>

using namespace android;
 class PreviewThread : public Thread {
        //CameraHardware* mHardware;
    public:
        PreviewThread(/*CameraHardware* hw*/)
            : Thread(false)/*, mHardware(hw)*/ { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            //mHardware->previewThread();
            // loop until we need to quit
            return true;
        }
    };
class V4L2CameraHal : public RefBase
{
    V4L2Camera              camera;

    // protected by mLock
    sp<PreviewThread>       mPreviewThread;
    static CameraParameters mParameters;
    static int32_t mMsgEnabled;
/**
     * Enable a message, or set of messages.
     */
    virtual void        enableMsgType(int32_t msgType);

    /**
     * Disable a message, or a set of messages.
     */
    virtual void        disableMsgType(int32_t msgType);

    /**
     * Query whether a message, or a set of messages, is enabled.
     * Note that this is operates as an AND, if any of the messages
     * queried are off, this will return false.
     */
    virtual bool        msgTypeEnabled(int32_t msgType);

    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual void release();
    virtual status_t sendCommand(int32_t cmd, int32_t arg1, int32_t arg2);
private:
 virtual             ~CameraHardware();

    static wp<CameraHardwareInterface> singleton;

    static const int kBufferCount = 4;

    class PreviewThread : public Thread {
        CameraHardware* mHardware;
    public:
        PreviewThread(CameraHardware* hw)
            : Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThread();
            // loop until we need to quit
            return true;
        }
    };

    void initDefaultParameters();
    bool initHeapLocked();

    int previewThread();

    static int beginAutoFocusThread(void *cookie);
    int autoFocusThread();

    static int beginPictureThread(void *cookie);
    int pictureThread();

    mutable Mutex           mLock;

    int                     mCameraId;
    CameraParameters        mParameters;

    sp<MemoryHeapBase>      mHeap;
    sp<MemoryBase>          mBuffer;

    sp<MemoryHeapBase>      mPreviewHeap;
    sp<MemoryHeapBase>      mRawHeap;
    sp<MemoryHeapBase>      mRecordHeap;
    sp<MemoryBase>          mRecordBuffer;

    bool                    mPreviewRunning;
    bool                    mRecordRunning;
    int                     mPreviewFrameSize;

    // protected by mLock
    sp<PreviewThread>       mPreviewThread;

    // only used from PreviewThread
    int                     mCurrentPreviewFrame;

    void *                  framebuffer;
    bool                    previewStopped;
    int                     camera_device;
    void*                   mem[4];
    int                     nQueued;
    int                     nDequeued;
    V4L2Camera              camera;
    notify_callback         mNotifyFn;
    data_callback           mDataFn;
    data_callback_timestamp mTimestampFn;
    void*                   mUser;
    int32_t                 mMsgEnabled;


}
