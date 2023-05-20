#include "pch.h"

#include "options.h"
#include "platform_data.h"
#include "platform_plugin.h"
#include "vulkan/VulkanGraphicsContext.h"
#include "program.h"
#include <gst/gst.h>
#include <gio/gio.h>

struct AndroidAppState {
    ANativeWindow *NativeWindow = nullptr;
    bool Resumed = false;
};

/**
 * Process the next main command.
 */
static void app_handle_cmd(struct android_app *app, int32_t cmd) {
    auto *appState = (AndroidAppState *) app->userData;

    switch (cmd) {
        // There is no APP_CMD_CREATE. The ANativeActivity creates the
        // application thread from onCreate(). The application thread
        // then calls android_main().
        case APP_CMD_START: {
            LOG_INFO("    APP_CMD_START");
            LOG_INFO("onStart()");
            break;
        }
        case APP_CMD_RESUME: {
            LOG_INFO("onResume()");
            LOG_INFO("    APP_CMD_RESUME");
            appState->Resumed = true;
            break;
        }
        case APP_CMD_PAUSE: {
            LOG_INFO("onPause()");
            LOG_INFO("    APP_CMD_PAUSE");
            appState->Resumed = false;
            break;
        }
        case APP_CMD_STOP: {
            LOG_INFO("onStop()");
            LOG_INFO("    APP_CMD_STOP");
            break;
        }
        case APP_CMD_DESTROY: {
            LOG_INFO("onDestroy()");
            LOG_INFO("    APP_CMD_DESTROY");
            appState->NativeWindow = nullptr;
            break;
        }
        case APP_CMD_INIT_WINDOW: {
            LOG_INFO("surfaceCreated()");
            LOG_INFO("    APP_CMD_INIT_WINDOW");
            appState->NativeWindow = app->window;
            break;
        }
        case APP_CMD_TERM_WINDOW: {
            LOG_INFO("surfaceDestroyed()");
            LOG_INFO("    APP_CMD_TERM_WINDOW");
            appState->NativeWindow = nullptr;
            break;
        }
        default :
            break;
    }
}

void ShowHelp() {
    LOG_INFO("adb shell setprop debug.xr.graphicsPlugin Vulkan");
    LOG_INFO("adb shell setprop debug.xr.formFactor Hmd|Handheld");
    LOG_INFO("adb shell setprop debug.xr.viewConfiguration Stereo|Mono");
    LOG_INFO("adb shell setprop debug.xr.blendMode Opaque|Additive|AlphaBlend");
}

bool UpdateOptionsFromSystemProperties(Options &options) {
    options.GraphicsPlugin = "Vulkan";

    char value[PROP_VALUE_MAX] = {};
    if (__system_property_get("debug.xr.graphicsPlugin", value) != 0) {
        options.GraphicsPlugin = value;
    }

    if (__system_property_get("debug.xr.formFactor", value) != 0) {
        options.FormFactor = value;
    }

    if (__system_property_get("debug.xr.viewConfiguration", value) != 0) {
        options.ViewConfiguration = value;
    }

    if (__system_property_get("debug.xr.blendMode", value) != 0) {
        options.EnvironmentBlendMode = value;
    }

    try {
        options.ParseStrings();
    } catch (std::invalid_argument &ia) {
        LOG_ERROR("%s", ia.what());
        ShowHelp();
        return false;
    }
    return true;
}

void android_main(struct android_app *app) {
    try {
        JNIEnv *Env;
        app->activity->vm->AttachCurrentThread(&Env, nullptr);

        AndroidAppState appState = {};

        app->userData = &appState;
        app->onAppCmd = app_handle_cmd;

        std::shared_ptr<Options> options = std::make_shared<Options>();

        std::shared_ptr<PlatformData> data = std::make_shared<PlatformData>();
        data->applicationVM = app->activity->vm;
        data->applicationActivity = app->activity->clazz;

        bool requestRestart = false;
        bool exitRenderLoop = false;

        std::shared_ptr<IPlatformPlugin> platformPlugin = CreatePlatformPlugin(data);
        auto graphicsContext = std::make_shared<VulkanEngine::VulkanGraphicsContext>(options);
        std::shared_ptr<OpenXrProgram> program = std::make_shared<OpenXrProgram>(options, platformPlugin, graphicsContext);

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

        options->SetEnvironmentBlendMode(program->GetPreferredBlendMode());
        UpdateOptionsFromSystemProperties(*options);

        program->InitializeDevice();
        program->InitializeSession();
        program->InitializeRendering();

        while (!app->destroyRequested) {
            for (;;) {
                int events;
                struct android_poll_source *source;

                // If the timeout is zero, returns immediately without blocking.
                // If the timeout is negative, waits indefinitely until an event appears.
                const int timeoutMilliseconds =
                        (!appState.Resumed && !program->IsSessionRunning() &&
                         app->destroyRequested == 0) ? -1 : 0;
                if (ALooper_pollAll(timeoutMilliseconds, nullptr, &events, (void **) &source) < 0) {
                    break;
                }

                if (source != nullptr) {
                    source->process(app, source);
                }
            }

            program->PollEvents(&exitRenderLoop, &requestRestart);
            if (exitRenderLoop) {
                ANativeActivity_finish(app->activity);
                continue;
            }

            if (!program->IsSessionRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                continue;
            }

            program->PollActions();
            program->SendControllerDatagram();
            program->RenderFrame();
        }

        app->activity->vm->DetachCurrentThread();
    } catch (const std::exception &ex) {
        LOG_ERROR("%s", ex.what());
    } catch (...) {
        LOG_ERROR("Unknown Error");
    }
}