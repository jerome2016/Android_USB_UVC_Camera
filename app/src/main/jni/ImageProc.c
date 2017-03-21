#include "ImageProc.h"
#include "DebugLog.h"
#include "utils.h"
#include "jpegDecode.h"
#include <pthread.h>

static int errnoExit(const char *s);
static int xioctl(int fd, int request, void *arg);
static int openDevice(UsbCamera *cam);
static int initDevice(UsbCamera *cam);
static int initMmap(UsbCamera *cam);
static int startCapturing(UsbCamera *cam);
static int readFrameOnce(UsbCamera *cam);
static int readFrame(UsbCamera *cam);
static int stopCapturing(UsbCamera *cam);
static int uninitDevice(UsbCamera *cam);
static int closeDevice(UsbCamera *cam);

#ifdef __DEBUG_ENCODE__
static int preview_count_debug = 0;
static int encode_count_debug = 0;
#endif

static JavaVM *gJavaVM = NULL;
static jobject gInterfaceObject = 0;
static UsbCamera *pUsbCamera = NULL;
static pthread_t record_id;

static int errnoExit(const char *s) {
	LOGE("%s error %d, %s", s, errno, strerror (errno));
	return ERROR_LOCAL;
}


static int xioctl(int fd, int request, void *arg) {

	int r;

	do {
		r = ioctl (fd, request, arg);
	}while (-1 == r && EINTR == errno);

	return r;
}


static int openDevice(UsbCamera *cam) {
	struct stat st;

	if (-1 == stat (cam->deviceName, &st)) {
		LOGE("Cannot identify '%s': %d, %s", cam->deviceName, errno, strerror (errno));
		return ERROR_DEVICE_NOT_EXIST;
	}

	if (!S_ISCHR (st.st_mode)) {
		LOGE("%s is no device", cam->deviceName);
		return ERROR_DEVICE_TYPE_ERROR;
	}

	cam->fd = open(cam->deviceName, O_RDWR| O_NONBLOCK, 0);

	if (-1 == cam->fd) {
		LOGE("Cannot open '%s': %d, %s", cam->deviceName, errno, strerror (errno));
		return ERROR_DEVICE_OPEN_FALIED;
	}
	return SUCCESSED;
}

static int initDevice(UsbCamera *cam) {

	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	struct v4l2_streamparm params;

	unsigned int min;
	struct v4l2_fmtdesc fmtdesc;

	if (-1 == xioctl (cam->fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			LOGE("%s is no V4L2 device", cam->deviceName);
			return ERROR_DEVICE_CAP_ERROR;
		} else {
			LOGE("%s error %d, %s\n", "VIDIOC_QUERYCAP", errno, strerror(errno));
			return ERROR_DEVICE_CAP_ERROR;
		}
	}
	
    LOGI("the camera device_name is %s \n", cam->deviceName);

    LOGI("the camera driver is %s\n", cap.driver);  
    LOGI("the camera card is %s\n", cap.card);//UVC Camera (046d:081b)  
    LOGI("the camera bus info is %s\n", cap.bus_info);  
    LOGI("the version is %d\n", cap.version);//199168 
    LOGI("the capabilities is %d\n", cap.capabilities);// 
    
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		LOGE("%s is no video capture device", cam->deviceName);
		return ERROR_DEVICE_CAP_ERROR;
	}

	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	while(ioctl(cam->fd,VIDIOC_ENUM_FMT,&fmtdesc)!=-1) {
	 LOGI("\t%d.%s\n",fmtdesc.index+1,fmtdesc.description);
	 LOGI("{ pixelformat = ''%c%c%c%c'', description = ''%s'' }\n",
        fmtdesc.pixelformat & 0xFF, (fmtdesc.pixelformat >> 8) & 0xFF,
        (fmtdesc.pixelformat >> 16) & 0xFF, (fmtdesc.pixelformat >> 24) & 0xFF,
        fmtdesc.description);
	 fmtdesc.index++;
	}
	
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		LOGE("%s does not support streaming i/o", cam->deviceName);
		return ERROR_DEVICE_CAP_ERROR;
	}

	CLEAR (cropcap);
	
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 == xioctl (cam->fd, VIDIOC_CROPCAP, &cropcap)) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect; 

		if (-1 == xioctl (cam->fd, VIDIOC_S_CROP, &crop)) {
			switch (errno) {
				case EINVAL:
					break;
				default:
					break;
			}
		}
	} else {
	}
	
	CLEAR (fmt);

	LOGI( "setfmt width=%d, height=%d...!\n", cam->width, cam->height);
	
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = cam->width;
	fmt.fmt.pix.height = cam->height;
	fmt.fmt.pix.pixelformat = cam->pixelFormat;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;

	if (-1 == xioctl(cam->fd, VIDIOC_S_FMT, &fmt)) {
		LOGE("%s error %d, %s\n", "VIDIOC_S_FMT", errno, strerror(errno));
		return ERROR_DEVICE_CAP_ERROR;
	}

    CLEAR(params);
    params.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    params.parm.capture.timeperframe.numerator = 1;
    params.parm.capture.timeperframe.denominator = 10;

	if(xioctl(cam->fd, VIDIOC_S_PARM, &params) == -1) {
		LOGE("%s error %d, %s\n", "VIDIOC_S_PARM", errno, strerror(errno));
		return ERROR_DEVICE_CAP_ERROR;
	}

	min = fmt.fmt.pix.width * 2;

	if (fmt.fmt.pix.bytesperline < min) {
		fmt.fmt.pix.bytesperline = min;
	}
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;

	if (fmt.fmt.pix.sizeimage < min) {
		fmt.fmt.pix.sizeimage = min;
	}

	return initMmap(cam);

}

static int initMmap(UsbCamera *cam) {

	struct v4l2_requestbuffers req;

	CLEAR (req);
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl (cam->fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			LOGE("%s does not support memory mapping", cam->deviceName);
			return ERROR_LOCAL;
		} else {
			LOGE("%s does not support memory mapping", cam->deviceName);
			return ERROR_REQBUFS;
		}
	}

	if (req.count < 2) {
		LOGE("Insufficient buffer memory on %s", cam->deviceName);
		return ERROR_LOCAL;
 	}

	cam->buffers =(struct buffer *)calloc(req.count, sizeof (*(cam->buffers)));

	if (!cam->buffers) {
		LOGE("Out of memory");
		return ERROR_LOCAL;
	}

	for (cam->n_buffers = 0; cam->n_buffers < req.count; ++cam->n_buffers) {

		struct v4l2_buffer buf;

		CLEAR (buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = cam->n_buffers;

		if (-1 == xioctl (cam->fd, VIDIOC_QUERYBUF, &buf)) {
			return ERROR_VIDIOC_QUERYBUF;
		}

		cam->buffers[cam->n_buffers].length = buf.length;
		cam->buffers[cam->n_buffers].start =
		mmap (NULL ,
			buf.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			cam->fd, buf.m.offset);

		if (MAP_FAILED == cam->buffers[cam->n_buffers].start) {
			return ERROR_MMAP_FAILD;
		}
	}

	return SUCCESSED;
}

static int startCapturing(UsbCamera *cam) {

	unsigned int i;
	enum v4l2_buf_type type;

	for (i = 0; i < cam->n_buffers; ++i) {

		struct v4l2_buffer buf;

		CLEAR (buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl (cam->fd, VIDIOC_QBUF, &buf)) {
			return ERROR_VIDIOC_QBUF;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl (cam->fd, VIDIOC_STREAMON, &type)) {
		return ERROR_VIDIOC_STREAMON;
	}

	return SUCCESSED;
}

static int readFrameOnce(UsbCamera *cam) {

    unsigned int count = 10;

    while (count-- > 0) {

		for (;;) {
			fd_set fds;
			struct timeval tv;
			int r = 0;

			FD_ZERO (&fds);
			FD_SET (cam->fd, &fds);

			tv.tv_sec = 2000;
			tv.tv_usec = 0;

			r = select(cam->fd + 1, &fds, NULL, NULL, &tv);

			if (-1 == r) {
				if (EINTR == errno) {
					continue;
				}

				return ERROR_SELECT;
			}

			if (0 == r) {
				LOGE("select timeout");
				return ERROR_LOCAL;

			}

			if (readFrame(cam) == 1) {
				break;
			}
		}

		return SUCCESSED;
    }
	return ERROR_LOCAL;
}

static int readFrame(UsbCamera *cam) {

	struct v4l2_buffer buf;
	int bufferSize = 0;

	CLEAR (buf);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl (cam->fd, VIDIOC_DQBUF, &buf)) {
		switch (errno) {
			case EAGAIN:
				return 0;
			case EIO:
			default:
				return ERROR_VIDIOC_DQBUF;
		}
	}

	assert (buf.index < cam->n_buffers);

	if(cam->pixelFormat == V4L2_PIX_FMT_MJPEG) {
		int width = 0;
		int height = 0;
		int ret = jpegDecode(&cam->framePreviewBuffer, (unsigned char *)cam->buffers[buf.index].start, &width, &height);
		bufferSize = width * height * 2;
		//LOGD("jpeg decode, ret=%d, width=%d, height=%d", ret, width, height);
	}else if(cam->pixelFormat == V4L2_PIX_FMT_YUYV){
		memcpy(cam->framePreviewBuffer, (unsigned char *)cam->buffers[buf.index].start, buf.bytesused);
		bufferSize = buf.bytesused;
	}else{
		LOGE("Invalid pixel format");
	}
	cam->frameBytesUsed = bufferSize;

	if (cam->isRecording && VIDEO_ENCODE_IDLE == cam->recordEncodeStatus) {
		memcpy(cam->yuv422Buffer, cam->framePreviewBuffer, bufferSize);
		cam->recordEncodeStatus = VIDEO_ENCODE_BUSY;
	}

	if (-1 == xioctl (cam->fd, VIDIOC_QBUF, &buf)) {
		return ERROR_VIDIOC_QBUF;
	}

	return 1;
}

static int stopCapturing(UsbCamera *cam) {

	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl (cam->fd, VIDIOC_STREAMOFF, &type)) {
		return ERROR_VIDIOC_STREAMOFF;
	}

	return SUCCESSED;

}

static int uninitDevice(UsbCamera *cam) {

	unsigned int i;

	for (i = 0; i < cam->n_buffers; ++i) {
		if (-1 == munmap(cam->buffers[i].start, cam->buffers[i].length)) {
			return ERROR_UNMMAP_FAILD;
		}
	}

	SAFE_FREE_ELEMENT (cam->buffers);
	SAFE_FREE_ELEMENT(cam->framePreviewBuffer);
	SAFE_FREE_ELEMENT(cam->yuv422Buffer);
	SAFE_FREE_ELEMENT(cam->rgbBuffer);
	return SUCCESSED;
}

static int closeDevice(UsbCamera *cam) {

	if (-1 == close (cam->fd)){
		cam->fd = -1;
		return errnoExit ("close");
	}

	cam->fd = -1;
	return SUCCESSED;
}


void pixelToBmp( JNIEnv* env,jobject bitmap){

	AndroidBitmapInfo info;
	void *pixels = NULL;
	int ret;
	int i;
	int *colors = NULL;

	int width = 0;
	int height = 0;

	if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
		LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
		return;
	}
    
	width = info.width;
	height = info.height;

	if(!pUsbCamera->rgbBuffer){
    	LOGE("the rgb is NULL \n");
		return;
	}

	if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
		LOGE("Bitmap format is not RGBA_8888 !");
		return;
	}

	if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
		LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
	}

	colors = (int*)pixels;
	int *lrgb = NULL;
	lrgb = &pUsbCamera->rgbBuffer[0];

	for(i=0; i<width*height; i++){
		*colors++ = *lrgb++;
	}

	AndroidBitmap_unlockPixels(env, bitmap);
}

static int mapCameraPixFormat(int pixelFormat){
	int format;
	switch(pixelFormat){
		case 1:
			format = V4L2_PIX_FMT_YUYV;
			break;
		case 0:
		default:
			format = V4L2_PIX_FMT_MJPEG;
			break;
	}
	return format;
}

int prepareCameraWithBase(int width, int height, int pixFormat){
	int ret;

	pUsbCamera = (UsbCamera *) malloc(sizeof(UsbCamera));
	memset(pUsbCamera,'\0', sizeof(UsbCamera));
	UsbCamera *pCamera = pUsbCamera;
	sprintf(pCamera->deviceName, "/dev/video%d", 0);
	pCamera->buffers = NULL;
	pCamera->width = width;
	pCamera->height = height;
	pCamera->isRecording = 0;
	pCamera->recordEncodeStatus = VIDEO_ENCODE_IDLE;
	pCamera->pixelFormat = mapCameraPixFormat(pixFormat);

	ret = openDevice(pCamera);

	if(ret != 0){
		LOGE("device open failed \n");
		return ret;
	}

	ret = initDevice(pCamera);
	LOGI("init camera return %d\n", ret);

	if(ret == SUCCESSED){
		ret = startCapturing(pCamera);

		if(ret != SUCCESSED){
			stopCapturing(pCamera);
			uninitDevice (pCamera);
			closeDevice (pCamera);
			LOGE("device resetted");
		}

	}

	if(ret == SUCCESSED){
		pCamera->framePreviewBuffer = (unsigned char *) calloc(1, pCamera->width * pCamera->height * 2);
		pCamera->yuv422Buffer = (unsigned char *) calloc(1, pCamera->width * pCamera->height * 2);
		pCamera->rgbBuffer = (int *)malloc(sizeof(int) * (pCamera->width * pCamera->height));
	}

#ifdef __DEBUG_ENCODE__
	preview_count_debug = 0;
	encode_count_debug = 0;
#endif
	
	return ret;
}

int processCamera(){
	int ret = 0;
	ret = readFrameOnce(pUsbCamera);
	yuyv422toABGRY(pUsbCamera->rgbBuffer, pUsbCamera->framePreviewBuffer, pUsbCamera->width, pUsbCamera->height);

#ifdef __DEBUG_ENCODE__
	preview_count_debug++;
#endif

	return ret;
}

void stopCamera(JNIEnv *env){
	if (pUsbCamera->isRecording) {
		stopRecord(env);
	}
	stopCapturing (pUsbCamera);
	uninitDevice (pUsbCamera);
	closeDevice (pUsbCamera);
	SAFE_FREE_ELEMENT(pUsbCamera);
}

void *videoRecordThread(void *pVal) {

	JNIEnv *env = NULL;
	JavaVM *vm = gJavaVM;
	UsbCamera *pCamera = pUsbCamera;
	unsigned char *yuv420spBuffer = NULL;
	const int Yuv420spSize = pCamera->width * pCamera->height * 3 / 2;

	LOGD("video record thread >>>>");
	
	if (gJavaVM == NULL) {
		LOGE("global vm is null");
		goto Exit;
	}
	int status = (*vm)->AttachCurrentThread(vm, &env, NULL);
	if(status < 0) {
		LOGE("attach video thread fail");
		goto Exit;
	}

 	jclass cls =  (*env)->GetObjectClass(env, gInterfaceObject);
	jmethodID mid = (*env)->GetStaticMethodID(env, cls, "encodeYuv2H264", "([B)V");
	if (mid == NULL) {
		LOGE("get static method id fail");
		goto Exit;
	}

	yuv420spBuffer = (unsigned char *) calloc(1, Yuv420spSize);

	while(pCamera->isRecording) {

			if(VIDEO_ENCODE_IDLE == pCamera->recordEncodeStatus){
				usleep(1000);
				continue;
			}

			yuv422Toyuv420sp(yuv420spBuffer, pCamera->yuv422Buffer, pCamera->width, pCamera->height);

			jbyteArray jarray = (*env)->NewByteArray(env, Yuv420spSize);
			(*env)->SetByteArrayRegion(env, jarray, 0, Yuv420spSize, (jbyte*)yuv420spBuffer);
			(*env)->CallStaticVoidMethod(env, cls, mid, jarray);
			(*env)->DeleteLocalRef(env, jarray);

			pCamera->recordEncodeStatus = VIDEO_ENCODE_IDLE;
		#ifdef __DEBUG_ENCODE__
			encode_count_debug++;
			LOGD("preview=%d, video=%d", preview_count_debug, encode_count_debug);
		#endif
	}

	SAFE_FREE_ELEMENT(yuv420spBuffer);
	(*vm)->DetachCurrentThread(vm);
	LOGD("video record thread <<<<");
Exit:
	return ((void *)0);
}

int startRecord(JNIEnv *env, jobject thiz){

	if(pUsbCamera->isRecording){
		return 0;
	}
	(*env)->GetJavaVM(env, &gJavaVM);
	gInterfaceObject = (*env)->NewGlobalRef(env, thiz);
	pUsbCamera->isRecording = 1;
	int ret = pthread_create(&record_id, NULL, videoRecordThread, NULL);
	return ret;
}

int stopRecord(JNIEnv *env){
	pUsbCamera->isRecording = 0;
	pthread_join(record_id, NULL);
	if(gInterfaceObject != 0){
		(*env)->DeleteGlobalRef(env, gInterfaceObject);
		gInterfaceObject = 0;
	}
	LOGD("stopRecord");
	return 0;
}
