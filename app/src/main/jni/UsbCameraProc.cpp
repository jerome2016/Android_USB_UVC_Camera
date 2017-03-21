#include <jni.h>
#include <assert.h>

#include "DebugLog.h"
#include "ImageProc.h"

#ifndef NELEM
# define NELEM(x) ((int) (sizeof(x) / sizeof((x)[0])))
#endif

static const char *gClassName = "jerome/com/usbcamera/ImageProc";

static jint native_prepare_camera(JNIEnv *env, jobject thiz, int width, int height, int pixelFormat){
	return prepareCameraWithBase(width, height, pixelFormat);
}

static jint native_process_camera(JNIEnv *env, jobject thiz){
	return processCamera();
}

static jint native_stop_camera(JNIEnv *env, jobject thiz){
	stopCamera(env);
	return 0;
}

static void native_pixel_to_bmp(JNIEnv *env, jobject thiz, jobject bmp){
	pixelToBmp(env, bmp);
}

static jint native_start_record(JNIEnv *env, jobject thiz){
	return startRecord(env, thiz);
}

static jint native_stop_record(JNIEnv *env, jobject thiz){
	return stopRecord(env);
}

static JNINativeMethod gMethods[] = {
	{"nativePrepareCamera",	"(III)I",	(void *)native_prepare_camera},
	{"nativeProcessCamera",	"()I",	(void *)native_process_camera},
	{"nativeStopCamera",	"()I",	(void *)native_stop_camera},
	{"nativePixelToBmp",	"(Landroid/graphics/Bitmap;)V",	(void *)native_pixel_to_bmp},
	{"nativeStartRecord",	"()I",	(void *)native_start_record},
	{"nativeStopRecord",	"()I",	(void *)native_stop_record},
};

static jint registerNativeMethods(JNIEnv* env, const char* className, JNINativeMethod* methods, int numMethods) {

    jclass clazz;

	clazz = env->FindClass(className);

	if(env->ExceptionCheck()){
		env->ExceptionDescribe();
		env->ExceptionClear();
	}

	if (clazz == NULL) {
		LOGE("Native registration unable to find class '%s'", className);
		return JNI_FALSE;
	}

	if (env->RegisterNatives(clazz, methods, numMethods) < 0) {
		LOGE("RegisterNatives failed for '%s'", className);
		return JNI_FALSE;
	}

	return JNI_TRUE;
}

static jint registerNatives(JNIEnv* env){

	jint ret = JNI_FALSE;

	if (registerNativeMethods(env, gClassName, gMethods, NELEM(gMethods))){
		ret = JNI_TRUE;
	}

	return ret;
}

void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
}

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
	JNIEnv* env = NULL;
	jint result = -1;
	
	if (vm->GetEnv((void**) &env, JNI_VERSION_1_4) != JNI_OK) {
		LOGE("ERROR: GetEnv failed\n");
		goto bail;
	}

	assert(env != NULL);

	if (registerNatives(env) != JNI_TRUE) {
		LOGE("ERROR: registerNatives failed");
		goto bail;
	}

	result = JNI_VERSION_1_4;
bail:
	return result;
}
