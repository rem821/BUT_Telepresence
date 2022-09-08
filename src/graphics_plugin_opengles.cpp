#include "pch.h"
#include "graphics_plugin.h"
#include "platform_plugin.h"

struct OpenGLESGraphicsPlugin : public IGraphicsPlugin {
    OpenGLESGraphicsPlugin() {}

    ~OpenGLESGraphicsPlugin() override {}

    std::vector <std::string> GetInstanceExtensions() const override {
        return {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    }
};

std::shared_ptr <IGraphicsPlugin>
CreateGraphicsPlugin() {
    return std::make_shared<OpenGLESGraphicsPlugin>();
}