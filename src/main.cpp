#include <android/log.h>
#include <android_native_app_glue.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <exception>
#include <memory>

#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, "oculus_openxr_poc", __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "oculus_openxr_poc", __VA_ARGS__)

void android_main(struct android_app *app) {
  try {
    JNIEnv *Env;
    app->activity->vm->AttachCurrentThread(&Env, nullptr);

    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    if (XR_SUCCEEDED(xrGetInstanceProcAddr(XR_NULL_HANDLE,
                                           "xrInitializeLoaderKHR",
                                           (PFN_xrVoidFunction *) (&initializeLoader)))) {
      XrLoaderInitInfoAndroidKHR loaderInitInfoAndroid;
      memset(&loaderInitInfoAndroid, 0, sizeof(loaderInitInfoAndroid));
      loaderInitInfoAndroid.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
      loaderInitInfoAndroid.next = nullptr;
      loaderInitInfoAndroid.applicationVM = app->activity->vm;
      loaderInitInfoAndroid.applicationContext = app->activity->clazz;
      initializeLoader((const XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfoAndroid);
    }

    while(true) {
      for (;;) {
        int events;
        struct android_poll_source *source;

        if (ALooper_pollAll(0, nullptr, &events, (void**)&source) < 0) {
          break;
        }

        if (source != nullptr) {
          source->process(app, source);
        }
      }
    }
  } catch (const std::exception &ex) {
    LOG_ERROR("%s", ex.what());
  } catch (...) {
    LOG_ERROR("Unknown Error");
  }
}