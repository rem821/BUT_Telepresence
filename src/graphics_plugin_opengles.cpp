#include "pch.h"
#include "check.h"
#include "log.h"
#include "graphics_plugin.h"
#include "platform_plugin.h"
#include "gpu_window.h"

#include <GLES3/gl3.h>
#include <GLES3/gl32.h>

struct OpenGLESGraphicsPlugin : public IGraphicsPlugin {
    OpenGLESGraphicsPlugin() {}

    ~OpenGLESGraphicsPlugin() override {}

    std::vector<std::string> GetInstanceExtensions() const override {
        return {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    }

    void
    DebugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                         const GLchar *message) {
        LOG_INFO("GLES Debug: %s", std::string(message, 0, length).c_str());
    }

    void InitializeDevice(XrInstance instance, XrSystemId systemId) override {
        PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnXrGetOpenGLESGraphicsRequirementsKHR = nullptr;
        CHECK_XRCMD(xrGetInstanceProcAddr(instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                                          reinterpret_cast<PFN_xrVoidFunction *>(&pfnXrGetOpenGLESGraphicsRequirementsKHR)));
        XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{
                XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        CHECK_XRCMD(
                pfnXrGetOpenGLESGraphicsRequirementsKHR(instance, systemId, &graphicsRequirements));

        m_window = CreateGpuWindow();
        m_window->Init();

        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);

        LOG_INFO("Required OpenGL ES Version: %s ~ %s",
                 GetXrVersionString(graphicsRequirements.minApiVersionSupported).c_str(),
                 GetXrVersionString(graphicsRequirements.maxApiVersionSupported).c_str());

        LOG_INFO("Using OpenGLES %d.%d", major, minor);

        const XrVersion desiredApiVersion = XR_MAKE_VERSION(major, minor, 0);
        if (graphicsRequirements.minApiVersionSupported > desiredApiVersion) {
            THROW("Runtime does not support desired Graphics API and/or version");
        }

        m_graphicsBinding.display = m_window->m_display;
        m_graphicsBinding.config = (EGLConfig) 0;
        m_graphicsBinding.context = m_window->m_context;

        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(
                [](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                   const GLchar *message, const void *userParam) {
                    ((OpenGLESGraphicsPlugin *) userParam)->DebugMessageCallback(source, type, id,
                                                                                 severity, length,
                                                                                 message);
                }, this);
    }

    std::vector<XrSwapchainImageBaseHeader *> AllocateSwapchainImageStructs(
            uint32_t capacity,
            const XrSwapchainCreateInfo &swapchainCreateInfo
    ) override {
        std::vector<XrSwapchainImageOpenGLESKHR> swapchainImageBuffer(capacity);
        std::vector<XrSwapchainImageBaseHeader *> swapchainImageBase;
        for (XrSwapchainImageOpenGLESKHR &image: swapchainImageBuffer) {
            image.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
            swapchainImageBase.push_back(reinterpret_cast<XrSwapchainImageBaseHeader *>(&image));
        }

        // Keep the buffer alive by moving it into the list of buffers
        m_swapchainImageBuffers.push_back(std::move(swapchainImageBuffer));

        return swapchainImageBase;
    }


    int64_t SelectColorSwapchainFormat(const std::vector<int64_t> &runtimeFormats) const override {
        std::vector<int64_t> supportedColorSwapchainFormats{
                GL_RGBA8, GL_RGBA8_SNORM, GL_SRGB8_ALPHA8};

        auto swapchainFormatIt = std::find_first_of(runtimeFormats.begin(), runtimeFormats.end(),
                                                    supportedColorSwapchainFormats.begin(),
                                                    supportedColorSwapchainFormats.end());
        if (swapchainFormatIt == runtimeFormats.end()) {
            THROW("No runtime swapchain format supprted for color swapchain");
        }

        return *swapchainFormatIt;
    }

    const XrBaseInStructure *GetGraphicsBinding() const override {
        return reinterpret_cast<const XrBaseInStructure *>(&m_graphicsBinding);
    }

    void SetBlendMode(XrEnvironmentBlendMode blendMode) override {
        static const std::array<float, 4> SlateGrey{0.184313729f, 0.309803933f, 0.309803933f, 1.0f};
        static const std::array<float, 4> TransparentBlack{0.0f, 0.0f, 0.0f, 0.0f};
        static const std::array<float, 4> Black{0.0f, 0.0f, 0.0f, 1.0f};

        switch (blendMode) {
            case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
                m_clearColor = SlateGrey;
                break;
            case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
                m_clearColor = Black;
                break;
            case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
                m_clearColor = TransparentBlack;
                break;
            default:
                Throw("Unexpected Blend Mode", nullptr, FILE_AND_LINE);
        }
    }

    uint32_t GetSupportedSwapchainSampleCount(const XrViewConfigurationView &view) override {
        return 1;
    }

private:
    std::list<std::vector<XrSwapchainImageOpenGLESKHR>> m_swapchainImageBuffers;
    std::array<float, 4> m_clearColor;
    std::shared_ptr<IGpuWindow> m_window;
    XrGraphicsBindingOpenGLESAndroidKHR m_graphicsBinding{
            XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
};

std::shared_ptr<IGraphicsPlugin>
CreateGraphicsPlugin() {
    return std::make_shared<OpenGLESGraphicsPlugin>();
}