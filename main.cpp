#include <android_native_app_glue.h>
#include <android/log.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "oculus_openxr_poc", __VA_ARGS__)

void android_main(struct android_app *app) {
    LOG("Hello World");
}