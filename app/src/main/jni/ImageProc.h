
#ifndef __IMAGE_PROC_H__
#define __IMAGE_PROC_H__

#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>

#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>          /* for videodev2.h */

#include <linux/videodev2.h>
#include <linux/usbdevice_fs.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAFE_FREE_ELEMENT(hp) if(hp != NULL){free(hp); hp = NULL;}
#define CLEAR(x) memset (&(x), 0, sizeof (x))

#define ERROR_DEVICE_NOT_EXIST -1
#define ERROR_DEVICE_TYPE_ERROR -2
#define ERROR_DEVICE_OPEN_FALIED -3
#define ERROR_PARAM -4
#define ERROR_DEVICE_CAP_ERROR -5

#define ERROR_VIDIOC_QUERYBUF -6
#define ERROR_VIDIOC_QBUF -7
#define ERROR_VIDIOC_DQBUF -70
#define ERROR_REQBUFS -71


#define ERROR_MMAP_FAILD -8
#define ERROR_UNMMAP_FAILD -88
#define ERROR_LOCAL -9
#define ERROR_VIDIOC_STREAMON -10
#define ERROR_VIDIOC_STREAMOFF -11
#define ERROR_SELECT -12

#define SUCCESSED 0

enum{
	VIDEO_ENCODE_IDLE = 0,
	VIDEO_ENCODE_BUSY = 1
};

struct buffer {
	void *start;
	size_t length;
};

typedef struct camera {
	char deviceName[32];
	int fd;
	int width;
	int height;
	struct buffer *buffers;
	int n_buffers;
	unsigned char *framePreviewBuffer;
	int frameBytesUsed;
	unsigned char *yuv422Buffer;
	int *rgbBuffer;
	int isRecording;
	int recordEncodeStatus;
	int pixelFormat;
}UsbCamera;

int prepareCameraWithBase(int width, int height, int pixelFormat);
int processCamera();
void stopCamera(JNIEnv *env);
void pixelToBmp( JNIEnv* env, jobject bitmap);
int startRecord(JNIEnv *env, jobject thiz);
int stopRecord(JNIEnv *env);

#ifdef __cplusplus
}
#endif

#endif
