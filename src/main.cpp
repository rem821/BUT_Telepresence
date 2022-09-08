#include "pch.h"

#include "log.h"
#include "platform_data.h"
#include "platform_plugin.h"
#include "graphics_plugin.h"
#include "program.h"

void android_main(struct android_app *app) {
    try {
        JNIEnv *Env;
        app->activity->vm->AttachCurrentThread(&Env, nullptr);

        std::shared_ptr<PlatformData> data = std::make_shared<PlatformData>();
        data->applicationVM = app->activity->vm;
        data->applicationActivity = app->activity->clazz;

        std::shared_ptr<IPlatformPlugin> platformPlugin = CreatePlatformPlugin(data);
        std::shared_ptr<IGraphicsPlugin> graphicsPlugin = CreateGraphicsPlugin();

        std::shared_ptr<IOpenXrProgram> program = CreateOpenXrProgram(platformPlugin,
                                                                      graphicsPlugin);

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
            initializeLoader((const XrLoaderInitInfoBaseHeaderKHR *) &loaderInitInfoAndroid);
        }

        program->CreateInstance();
        program->InitializeSystem();

        while (true) {
            for (;;) {
                int events;
                struct android_poll_source *source;

                if (ALooper_pollAll(0, nullptr, &events, (void **) &source) < 0) {
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