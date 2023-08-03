#include "pch.h"
#include "check.h"
#include "log.h"
#include "graphics_plugin.h"
#include "platform_plugin.h"
#include "gpu_window.h"
#include "geometry.h"
#include "linear.h"

#include <GLES3/gl3.h>
#include <GLES3/gl32.h>

static const char *VertexShaderGlsl = R"_(#version 320 es

    in vec3 position;
    in lowp vec2 texCoord;

    out lowp vec2 v_TexCoord;

    uniform mat4 u_ModelViewProjection;

    void main() {
       gl_Position = u_ModelViewProjection * vec4(position, 1.0);
       v_TexCoord = texCoord;
    }
    )_";

static const char *FragmentShaderGlsl = R"_(#version 320 es
    in lowp vec2 v_TexCoord;

    out lowp vec4 color;

    uniform sampler2D u_Texture;

    void main() {
        color = texture(u_Texture, v_TexCoord);
    }
    )_";

struct OpenGLESGraphicsPlugin : public IGraphicsPlugin {
    OpenGLESGraphicsPlugin() = default;

    ~OpenGLESGraphicsPlugin() override = default;

    [[nodiscard]] std::vector<std::string> GetInstanceExtensions() const override {
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
            THROW("Runtime does not support desired Graphics API and/or version")
        }

        m_graphicsBinding.display = m_window->m_display;
        m_graphicsBinding.config = nullptr;
        m_graphicsBinding.context = m_window->m_context;

        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback([](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam) {
            ((OpenGLESGraphicsPlugin *) userParam)->DebugMessageCallback(source, type, id, severity, length, message);
        }, this);

        InitializeResources();
    }

    void InitializeResources() {
        glGenFramebuffers(1, &m_swapchainFramebuffer);

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &VertexShaderGlsl, nullptr);
        glCompileShader(vertexShader);
        CheckShader(vertexShader);

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &FragmentShaderGlsl, nullptr);
        glCompileShader(fragmentShader);
        CheckShader(fragmentShader);

        m_program = glCreateProgram();
        glAttachShader(m_program, vertexShader);
        glAttachShader(m_program, fragmentShader);
        glLinkProgram(m_program);
        CheckProgram(m_program);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        m_modelViewProjectionUniformLocation = glGetUniformLocation(m_program,"u_ModelViewProjection");
        m_texture2DUniformLocation = glGetUniformLocation(m_program,"u_Texture");

        m_vertexAttribCoords = glGetAttribLocation(m_program, "position");
        m_vertexAttribTexCoord = glGetAttribLocation(m_program, "texCoord");

        glGenBuffers(1, &m_cubeVertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Geometry::c_quadVertices), Geometry::c_quadVertices, GL_STATIC_DRAW);

        glGenBuffers(1, &m_cubeIndexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeIndexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Geometry::c_quadIndices), Geometry::c_quadIndices, GL_STATIC_DRAW);

        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);
        glEnableVertexAttribArray(m_vertexAttribCoords);
        glEnableVertexAttribArray(m_vertexAttribTexCoord);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVertexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeIndexBuffer);
        glVertexAttribPointer(m_vertexAttribCoords, 3, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex), nullptr);
        glVertexAttribPointer(m_vertexAttribTexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex), reinterpret_cast<const void *>(sizeof(XrVector3f)));

        glGenTextures(1, &m_texture2D);
        glBindTexture(GL_TEXTURE_2D, m_texture2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }


    static void CheckShader(GLuint shader) {
        GLint r = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &r);
        if (r == GL_FALSE) {
            GLchar msg[4096] = {};
            GLsizei length;
            glGetShaderInfoLog(shader, sizeof(msg), &length, msg);
            THROW(Fmt("Compile shader failed: %s", msg))
        }
    }

    static void CheckProgram(GLuint program) {
        GLint r = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &r);
        if (r == GL_FALSE) {
            GLchar msg[4096];
            GLsizei length;
            glGetProgramInfoLog(program, sizeof(msg), &length, msg);
            THROW(Fmt("Link program failed: %s", msg))
        }
    }

    std::vector<XrSwapchainImageBaseHeader *>
    AllocateSwapchainImageStructs(
            uint32_t
            capacity,
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


    [[nodiscard]] int64_t
    SelectColorSwapchainFormat(const std::vector<int64_t> &runtimeFormats) const override {
        std::vector<int64_t> supportedColorSwapchainFormats{
                GL_RGBA8, GL_RGBA8_SNORM, GL_SRGB8_ALPHA8};

        auto swapchainFormatIt = std::find_first_of(runtimeFormats.begin(), runtimeFormats.end(),
                                                    supportedColorSwapchainFormats.begin(),
                                                    supportedColorSwapchainFormats.end());
        if (swapchainFormatIt == runtimeFormats.end()) {
            THROW("No runtime swapchain format supprted for color swapchain")
        }

        return *swapchainFormatIt;
    }

    [[nodiscard]] const XrBaseInStructure *GetGraphicsBinding() const override {
        return reinterpret_cast<const XrBaseInStructure *>(&m_graphicsBinding);
    }

    void SetBlendMode(XrEnvironmentBlendMode blendMode) override {
        static const std::array<float, 4> SlateGrey{0.184313729f, 0.309803933f, 0.309803933f, 1.0f};
        static const std::array<float, 4> TransparentBlack{0.0f, 0.0f, 0.0f, 0.0f};
        static const std::array<float, 4> Black{0.0f, 0.0f, 0.0f, 1.0f};

        switch (blendMode) {
            case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
                m_clearColor = Black;
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

    uint32_t GetDepthTexture(uint32_t colorTexture) {
        auto depthBufferIt = m_colorToDepthMap.find(colorTexture);
        if (depthBufferIt != m_colorToDepthMap.end()) {
            return depthBufferIt->second;
        }

        GLint width;
        GLint height;
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

        uint32_t depthTexture;
        glGenTextures(1, &depthTexture);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT,
                     GL_UNSIGNED_INT, nullptr);

        m_colorToDepthMap.insert(std::make_pair(colorTexture, depthTexture));

        return depthTexture;
    }

    void RenderView(const XrCompositionLayerProjectionView &layerView,
                    const XrSwapchainImageBaseHeader *swapchainImage,
                    int64_t swapchainFormat, const Quad &quad, const void *image) override {
        CHECK(layerView.subImage.imageArrayIndex == 0)
        (void) swapchainFormat;

        glBindFramebuffer(GL_FRAMEBUFFER, m_swapchainFramebuffer);

        const uint32_t colorTexture = reinterpret_cast<const XrSwapchainImageOpenGLESKHR *>(swapchainImage)->image;

        glViewport(
                static_cast<GLint>(layerView.subImage.imageRect.offset.x),
                static_cast<GLint>(layerView.subImage.imageRect.offset.y),
                static_cast<GLint>(layerView.subImage.imageRect.extent.width),
                static_cast<GLint>(layerView.subImage.imageRect.extent.height)
        );

        glFrontFace(GL_CW);
        glCullFace(GL_BACK);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        const uint32_t depthTexture = GetDepthTexture(colorTexture);

        glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);

        glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glUseProgram(m_program);

        const auto &pose = layerView.pose;
        XrMatrix4x4f proj;
        XrMatrix4x4f_CreateProjectionFov(&proj, layerView.fov, 0.05f, 100.0f);
        XrMatrix4x4f toView;
        XrVector3f scale{1.f, 1.f, 1.f};
        XrMatrix4x4f_CreateTranslationRotationScale(&toView, &pose.position, &pose.orientation, &scale);
        XrMatrix4x4f view;
        XrMatrix4x4f_InvertRigidBody(&view, &toView);
        XrMatrix4x4f vp;
        XrMatrix4x4f_Multiply(&vp, &proj, &view);

        glBindVertexArray(m_vao);

        auto pos = XrVector3f{quad.Pose.position.x, quad.Pose.position.y - 0.3f, quad.Pose.position.z};
        XrMatrix4x4f model;
        XrMatrix4x4f_CreateTranslationRotationScale(&model, &pos, &quad.Pose.orientation, &quad.Scale);
        XrMatrix4x4f mvp;
        XrMatrix4x4f_Multiply(&mvp, &vp, &model);
        glUniformMatrix4fv(static_cast<GLint>(m_modelViewProjectionUniformLocation), 1, GL_FALSE, reinterpret_cast<const GLfloat *>(&mvp));
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ArraySize(Geometry::c_quadIndices)), GL_UNSIGNED_SHORT, nullptr);

        glBindTexture(GL_TEXTURE_2D, m_texture2D);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, image);

        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    uint32_t GetSupportedSwapchainSampleCount(const XrViewConfigurationView &view) override {
        return 1;
    }

private:
    std::list<std::vector<XrSwapchainImageOpenGLESKHR>> m_swapchainImageBuffers;

    GLuint m_swapchainFramebuffer{0};
    GLuint m_program{0};
    GLint m_modelViewProjectionUniformLocation{0};
    GLint m_texture2DUniformLocation{0};
    GLuint m_vertexAttribCoords{0};
    GLuint m_vertexAttribTexCoord{0};

    GLuint m_texture2D{0};

    int textureWidth = 1920;
    int textureHeight = 1080;
    std::vector<int> widths{textureWidth, textureWidth / 2, textureWidth / 2};
    std::vector<int> heights{textureHeight, textureHeight / 2, textureHeight / 2};

    GLuint m_vao{0};
    GLuint m_cubeVertexBuffer{0};
    GLuint m_cubeIndexBuffer{0};

    std::map<uint32_t, uint32_t> m_colorToDepthMap;
    std::array<float, 4> m_clearColor{};
    std::shared_ptr<IGpuWindow> m_window;
    XrGraphicsBindingOpenGLESAndroidKHR m_graphicsBinding{
            XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
};

std::shared_ptr<IGraphicsPlugin>
CreateGraphicsPlugin() {
    return std::make_shared<OpenGLESGraphicsPlugin>();
}