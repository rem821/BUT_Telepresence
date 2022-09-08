#include "pch.h"
#include "graphics_plugin.h"
#include "platform_plugin.h"

struct OpenGLESGraphicsPlugin : public IGraphicsPlugin {
    OpenGLESGraphicsPlugin() {}

    ~OpenGLESGraphicsPlugin() override {}

    std::vector <std::string> GetInstanceExtensions() const override {
        return {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
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
        }
    }

private:
    std::array<float, 4> m_clearColor;
};

std::shared_ptr <IGraphicsPlugin>
CreateGraphicsPlugin() {
    return std::make_shared<OpenGLESGraphicsPlugin>();
}