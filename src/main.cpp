#include "pch.h"

#include "log.h"
#include <gst/gst.h>
#include <gio/gio.h>
#include <chrono>

#include "program.h"

struct AndroidAppState {
    ANativeWindow *NativeWindow = nullptr;
    bool Resumed = false;
};

static void
ProcessAndroidCmd(struct android_app *app, int32_t cmd) {
    AndroidAppState *appState = (AndroidAppState *) app->userData;

    switch (cmd) {
        case APP_CMD_START:
            //LOGI ("APP_CMD_START");
            break;

        case APP_CMD_RESUME:
            //LOGI ("APP_CMD_RESUME");
            appState->Resumed = true;
            break;

        case APP_CMD_PAUSE:
            //LOGI ("APP_CMD_PAUSE");
            appState->Resumed = false;
            break;

        case APP_CMD_STOP:
            //LOGI ("APP_CMD_STOP");
            break;

        case APP_CMD_DESTROY:
            //LOGI ("APP_CMD_DESTROY");
            appState->NativeWindow = nullptr;
            break;

            // The window is being shown, get it ready.
        case APP_CMD_INIT_WINDOW:
            //LOGI ("APP_CMD_INIT_WINDOW");
            appState->NativeWindow = app->window;
            break;

            // The window is being hidden or closed, clean it up.
        case APP_CMD_TERM_WINDOW:
            //LOGI ("APP_CMD_TERM_WINDOW");
            appState->NativeWindow = nullptr;
            break;
    }
}

void android_main(struct android_app *app) {
    try {
        JNIEnv *Env;
        app->activity->vm->AttachCurrentThread(&Env, nullptr);

        AndroidAppState appState = {};
        app->userData = &appState;
        app->onAppCmd = ProcessAndroidCmd;

        std::unique_ptr<TelepresenceProgram> telepresenceProgram = std::make_unique<TelepresenceProgram>(
                app);

        bool requestRestart = false;
        bool exitRenderLoop = false;

        while (!app->destroyRequested) {
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

            telepresenceProgram->UpdateFrame();
        }

        app->activity->vm->DetachCurrentThread();
    } catch (const std::exception &ex) {
        LOG_ERROR("%s", ex.what());
    } catch (...) {
        LOG_ERROR("Unknown Error");
    }
}