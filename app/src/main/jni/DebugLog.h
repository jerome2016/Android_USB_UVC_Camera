#ifndef __DEBUG_LOG_H__
#define __DEBUG_LOG_H__

#define __THIRD_PARTY_APK__

#if defined(__THIRD_PARTY_APK__)

#include <android/log.h>

#define TAG "UsbCameraJNI"

#define __LOG_SUPPORT__

#if defined(__LOG_SUPPORT__)

#define LOGI(msg, ...)	__android_log_print(ANDROID_LOG_INFO, TAG, msg, ##__VA_ARGS__)
#define LOGE(msg, ...)	__android_log_print(ANDROID_LOG_ERROR, TAG, msg, ##__VA_ARGS__)
#define LOGW(msg, ...)	__android_log_print(ANDROID_LOG_WARN, TAG, msg, ##__VA_ARGS__)
#define LOGD(msg, ...)	__android_log_print(ANDROID_LOG_DEBUG, TAG, msg, ##__VA_ARGS__)

#else

#define LOGI(msg, ...)
#define LOGE(msg, ...)
#define LOGW(msg, ...)
#define LOGD(msg, ...)

#endif

#else

#include <utils/Log.h>

#endif/*__THIRD_PARTY_APK__*/

#endif/*__DEBUG_LOG_H__*/
