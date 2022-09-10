#include "pch.h"
#include "log.h"
#include "check.h"
#include "gpu_window.h"

static const char *EglErrorString(const EGLint error) {
    switch (error) {
        case EGL_SUCCESS:
            return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:
            return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:
            return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:
            return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:
            return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT:
            return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG:
            return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE:
            return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:
            return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE:
            return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH:
            return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER:
            return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP:
            return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:
            return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST:
            return "EGL_CONTEXT_LOST";
        default:
            return "unknown";
    }
}

struct GpuWindow : IGpuWindow {
    ~GpuWindow() override {}

    bool CreateContext() {
        // Color Format: B8G8R8A8, Depth 24 bit

        const int MAX_CONFIGS = 1024;
        EGLConfig configs[MAX_CONFIGS];
        EGLint numConfigs = 0;
        CHECK_EGL(eglGetConfigs(m_display, configs, MAX_CONFIGS, &numConfigs));

        const EGLint configAttribs[] = {
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 24,
                EGL_SAMPLE_BUFFERS, 0,
                EGL_SAMPLES, 0,
                EGL_NONE,
        };

        for (int i = 0; i < numConfigs; i++) {
            EGLint value = 0;

            eglGetConfigAttrib(m_display, configs[i], EGL_RENDERABLE_TYPE, &value);
            if ((value & EGL_OPENGL_ES3_BIT) != EGL_OPENGL_ES3_BIT) {
                continue;
            }

            eglGetConfigAttrib(m_display, configs[i], EGL_SURFACE_TYPE, &value);
            if ((value & (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) !=
                (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) {
                continue;
            }

            int j = 0;
            for (; configAttribs[j] != EGL_NONE; j += 2) {
                eglGetConfigAttrib(m_display, configs[i], configAttribs[j], &value);
                if (value != configAttribs[j + 1]) {
                    break;
                }
            }

            if (configAttribs[j] == EGL_NONE) {
                m_config = configs[i];
                break;
            }
        }

        if (m_config == nullptr) {
            LOG_ERROR("Failed to find EGLConfig");
            return false;
        }

        // TODO: Add gpu priority if needed
        EGLint contextAttribs[] = {
                EGL_CONTEXT_CLIENT_VERSION, 3,
                EGL_NONE
        };

        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttribs);
        if (m_context == EGL_NO_CONTEXT) {
            LOG_ERROR("eglCreateContext() failed: %s", EglErrorString(eglGetError()));
            return false;
        }

        const EGLint surfaceAttributes[] = {
                EGL_WIDTH, 16,
                EGL_HEIGHT, 16,
                EGL_NONE,
        };

        m_mainSurface = eglCreatePbufferSurface(m_display, m_config, surfaceAttributes);
        if (m_mainSurface == EGL_NO_SURFACE) {
            LOG_ERROR("eglCreatePbufferSurface() failed: %s", EglErrorString(eglGetError()));
            eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
            return false;
        }

        return true;
    }

    bool Init() override {

        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint majorVersion, minorVersion;
        CHECK_EGL(eglInitialize(m_display, &majorVersion, &minorVersion));

        LOG_INFO("EGL Version: %d.%d", majorVersion, minorVersion);

        if (!CreateContext()) return false;

        CHECK_EGL(eglMakeCurrent(m_display, m_mainSurface, m_mainSurface, m_context));

        return true;
    }
};

std::shared_ptr<IGpuWindow> CreateGpuWindow() {
    return std::make_shared<GpuWindow>();
}
