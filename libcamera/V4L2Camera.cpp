/*
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      Author: Niels Keeman <nielskeeman@gmail.com>
 *
 */

#define LOG_TAG "V4L2Camera"
#include <utils/Log.h>
#include <utils/threads.h>
#include <fcntl.h>

#include "V4L2Camera.h"

extern "C" { /* Android jpeglib.h missed extern "C" */
#include <jpeglib.h>
     void convertYUYVtoRGB565(unsigned char *buf, unsigned char *rgb, int width, int height);
}

namespace android {

V4L2Camera::V4L2Camera ()
    : nQueued(0), nDequeued(0)
{
    videoIn = (struct vdIn *) calloc (1, sizeof (struct vdIn));
}

V4L2Camera::~V4L2Camera()
{
    free(videoIn);
}

int V4L2Camera::Open (const char *device, int width, int height, int pixelformat)
{
    int ret;

    if ((fd = open(device, O_RDWR)) == -1) {
        LOGE("ERROR opening V4L interface: %s", strerror(errno));
    return -1;
    }

    ret = ioctl (fd, VIDIOC_QUERYCAP, &videoIn->cap);
    if (ret < 0) {
        LOGE("Error opening device: unable to query device.");
        return -1;
    }

    if ((videoIn->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        LOGE("Error opening device: video capture not supported.");
        return -1;
    }

    if (!(videoIn->cap.capabilities & V4L2_CAP_STREAMING)) {
        LOGE("Capture device does not support streaming i/o");
        return -1;
    }

    videoIn->width = width;
    videoIn->height = height;
    videoIn->framesizeIn = (width * height << 1);
    videoIn->formatIn = pixelformat;

    videoIn->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->format.fmt.pix.width = width;
    videoIn->format.fmt.pix.height = height;
    videoIn->format.fmt.pix.pixelformat = pixelformat;

    ret = ioctl(fd, VIDIOC_S_FMT, &videoIn->format);
    if (ret < 0) {
        LOGE("Open: VIDIOC_S_FMT Failed: %s", strerror(errno));
        return ret;
    }

    return 0;
}

void V4L2Camera::Close ()
{
    close(fd);
}

int V4L2Camera::Init()
{
    int ret;

    /* Check if camera can handle NB_BUFFER buffers */
    videoIn->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->rb.memory = V4L2_MEMORY_MMAP;
    videoIn->rb.count = NB_BUFFER;

    ret = ioctl(fd, VIDIOC_REQBUFS, &videoIn->rb);
    if (ret < 0) {
        LOGE("Init: VIDIOC_REQBUFS failed: %s", strerror(errno));
        return ret;
    }

    for (int i = 0; i < NB_BUFFER; i++) {

        memset (&videoIn->buf, 0, sizeof (struct v4l2_buffer));

        videoIn->buf.index = i;
        videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        videoIn->buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl (fd, VIDIOC_QUERYBUF, &videoIn->buf);
        if (ret < 0) {
            LOGE("Init: Unable to query buffer (%s)", strerror(errno));
            return ret;
        }

        videoIn->mem[i] = mmap (0,
               videoIn->buf.length,
               PROT_READ | PROT_WRITE,
               MAP_SHARED,
               fd,
               videoIn->buf.m.offset);

        if (videoIn->mem[i] == MAP_FAILED) {
            LOGE("Init: Unable to map buffer (%s)", strerror(errno));
            return -1;
        }

        ret = ioctl(fd, VIDIOC_QBUF, &videoIn->buf);
        if (ret < 0) {
            LOGE("Init: VIDIOC_QBUF Failed");
            return -1;
        }

        nQueued++;
    }

    return 0;
}

void V4L2Camera::Uninit ()
{
    int ret;

    videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->buf.memory = V4L2_MEMORY_MMAP;

    /* Dequeue everything */
    int DQcount = nQueued - nDequeued;

    for (int i = 0; i < DQcount-1; i++) {
        ret = ioctl(fd, VIDIOC_DQBUF, &videoIn->buf);
        if (ret < 0)
            LOGE("Uninit: VIDIOC_DQBUF Failed");
    }
    nQueued = 0;
    nDequeued = 0;

    /* Unmap buffers */
    for (int i = 0; i < NB_BUFFER; i++)
        if (munmap(videoIn->mem[i], videoIn->buf.length) < 0)
            LOGE("Uninit: Unmap failed");
}

int V4L2Camera::StartStreaming ()
{
    enum v4l2_buf_type type;
    int ret;

    if (!videoIn->isStreaming) {
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl (fd, VIDIOC_STREAMON, &type);
        if (ret < 0) {
            LOGE("StartStreaming: Unable to start capture: %s", strerror(errno));
            return ret;
        }

        videoIn->isStreaming = true;
    }

    return 0;
}

int V4L2Camera::StopStreaming ()
{
    enum v4l2_buf_type type;
    int ret;

    if (videoIn->isStreaming) {
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl (fd, VIDIOC_STREAMOFF, &type);
        if (ret < 0) {
            LOGE("StopStreaming: Unable to stop capture: %s", strerror(errno));
            return ret;
        }

        videoIn->isStreaming = false;
    }

    return 0;
}

void * V4L2Camera::GrabPreviewFrame ()
{
    unsigned char *tmpBuffer;
    int ret;

    videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->buf.memory = V4L2_MEMORY_MMAP;

    /* DQ */
    ret = ioctl(fd, VIDIOC_DQBUF, &videoIn->buf);
    if (ret < 0) {
        LOGE("GrabPreviewFrame: VIDIOC_DQBUF Failed");
        return NULL;
    }
    nDequeued++;
    return  videoIn->mem[videoIn->buf.index];
}

void V4L2Camera::ReleasePreviewFrame ()
{
    int ret;
    ret = ioctl(fd, VIDIOC_QBUF, &videoIn->buf);
    nQueued++;
    if (ret < 0) {
        LOGE("GrabPreviewFrame: VIDIOC_QBUF Failed");
        return;
    }
}


sp<IMemory> V4L2Camera::GrabRawFrame ()
{
    sp<MemoryHeapBase> memHeap = new MemoryHeapBase(videoIn->width * videoIn->height * 2);
    sp<MemoryBase> memBase = new MemoryBase(memHeap, 0, videoIn->width * videoIn->height * 2);

    // Not yet implemented, do I have to?

    return memBase;
}

// a helper class for jpeg compression in memory
class MemoryStream {
public:
    MemoryStream(char* buf, size_t sz);
    ~MemoryStream() { closeStream(); }

    void closeStream();
    size_t getOffset() const { return bytesWritten; }
    operator FILE *() { return file; }

private:
    static int runThread(void *);
    int readPipe();

    char *buffer;
    size_t size;
    size_t bytesWritten;
    int pipefd[2];
    FILE *file;
    Mutex lock;
    Condition exitedCondition;
};

MemoryStream::MemoryStream(char* buf, size_t sz)
    : buffer(buf), size(sz), bytesWritten(0)
{
    if ((file = pipe(pipefd) ? NULL : fdopen(pipefd[1], "w")))
        createThread(runThread, this);
}

void MemoryStream::closeStream()
{
    if (file) {
        AutoMutex l(lock);
        fclose(file);
        file = NULL;
        exitedCondition.wait(lock);
    }
}

int MemoryStream::runThread(void *self)
{
    return static_cast<MemoryStream *>(self)->readPipe();
}

int MemoryStream::readPipe()
{
    char *buf = buffer;
    ssize_t sz;
    while ((sz = read(pipefd[0], buf, size - bytesWritten)) > 0) {
        bytesWritten += sz;
        buf += sz;
    }
    close(pipefd[0]);
    AutoMutex l(lock);
    exitedCondition.signal();
    return 0;
}

camera_memory_t*  V4L2Camera::GrabJpegFrame (camera_request_memory   mRequestMemory)
{
    int ret;

    videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->buf.memory = V4L2_MEMORY_MMAP;

    /* Dequeue buffer */
    ret = ioctl(fd, VIDIOC_DQBUF, &videoIn->buf);
    if (ret < 0) {
        LOGE("GrabJpegFrame: VIDIOC_DQBUF Failed");
        return NULL;
    }
    nDequeued++;

    LOGI("GrabJpegFrame: Generated a frame from capture device");

    /* Enqueue buffer */
    ret = ioctl(fd, VIDIOC_QBUF, &videoIn->buf);
    if (ret < 0) {
        LOGE("GrabJpegFrame: VIDIOC_QBUF Failed");
        return NULL;
    }
    nQueued++;

    size_t bytesused = videoIn->buf.bytesused;
    if (char *tmpBuf = new char[bytesused]) {
        MemoryStream strm(tmpBuf, bytesused);
        saveYUYVtoJPEG((unsigned char *)videoIn->mem[videoIn->buf.index], videoIn->width, videoIn->height, strm, 100);
        strm.closeStream();
        size_t fileSize = strm.getOffset();
         camera_memory_t* picture = mRequestMemory(-1,fileSize,1,NULL);
        memcpy(picture->data, tmpBuf, fileSize);
        delete[] tmpBuf;

        return picture;
    }

    return NULL;
}

int V4L2Camera::saveYUYVtoJPEG (unsigned char *inputBuffer, int width, int height, FILE *file, int quality)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    unsigned char *line_buffer, *yuyv;
    int z;
    int fileSize;

    line_buffer = (unsigned char *) calloc (width * 3, 1);
    yuyv = inputBuffer;

    cinfo.err = jpeg_std_error (&jerr);
    jpeg_create_compress (&cinfo);
    jpeg_stdio_dest (&cinfo, file);

    LOGI("JPEG PICTURE WIDTH AND HEIGHT: %dx%d", width, height);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults (&cinfo);
    jpeg_set_quality (&cinfo, quality, TRUE);

    jpeg_start_compress (&cinfo, TRUE);

    z = 0;
    while (cinfo.next_scanline < cinfo.image_height) {
        int x;
        unsigned char *ptr = line_buffer;

        for (x = 0; x < width; x++) {
            int r, g, b;
            int y, u, v;

            if (!z)
                y = yuyv[0] << 8;
            else
                y = yuyv[2] << 8;

            u = yuyv[1] - 128;
            v = yuyv[3] - 128;

            r = (y + (359 * v)) >> 8;
            g = (y - (88 * u) - (183 * v)) >> 8;
            b = (y + (454 * u)) >> 8;

            *(ptr++) = (r > 255) ? 255 : ((r < 0) ? 0 : r);
            *(ptr++) = (g > 255) ? 255 : ((g < 0) ? 0 : g);
            *(ptr++) = (b > 255) ? 255 : ((b < 0) ? 0 : b);

            if (z++) {
                z = 0;
                yuyv += 4;
            }
        }

        row_pointer[0] = line_buffer;
        jpeg_write_scanlines (&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress (&cinfo);
    fileSize = ftell(file);
    jpeg_destroy_compress (&cinfo);

    free (line_buffer);

    return fileSize;
}

}; // namespace android
