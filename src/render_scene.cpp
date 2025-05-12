//
// Created by stand on 16.08.2024.
//
#include "pch.h"
#include <GLES3/gl3.h>
#include "util_shader.h"
#include "geometry.h"
#include "linear.h"
#include <random>
#include "render_imgui.h"
#include "util_render_target.h"
#include "render_texplate.h"

#include "render_scene.h"

static const std::array<float, 4> CLEAR_COLOR{0.05f, 0.05f, 0.05f, 1.0f};

static int GUI_WIDTH = 300;
static int GUI_HEIGHT = 420;

static GLuint cubeVertexBuffer{0}, cubeIndexBuffer{0}, vertexArrayObject{0},
        vertexAttribCoords{0}, vertexAttribTexCoords{0}, texture2D{0};

static shader_obj_t image_shader_object;
static shader_obj_t gui_shader_object;

static render_target_t gui_render_target;

static const char *ImageVertexShaderGlsl = R"_(#version 320 es

    in vec3 position;
    in lowp vec2 texCoord;

    out lowp vec2 v_TexCoord;

    uniform mat4 u_ModelViewProjection;

    void main() {
       gl_Position = u_ModelViewProjection * vec4(position, 1.0);
       v_TexCoord = texCoord;
    }
    )_";

static const char *ImageFragmentShaderGlsl = R"_(#version 320 es
    in lowp vec2 v_TexCoord;

    out lowp vec4 color;

    uniform sampler2D u_Texture;

    void main() {
        color = texture(u_Texture, v_TexCoord);
    }
    )_";

static const char *GuiVertexShaderGlsl = R"_(#version 320 es
    in vec3 position;
    in lowp vec4 color;
    out lowp vec4 v_color;

    uniform mat4 u_ModelViewProjection;

    void main(void) {
        gl_Position = u_ModelViewProjection * vec4(position, 1.0);
        v_color = color;
    }
    )_";

static const char *GuiFragmentShaderGlsl = R"_(#version 320 es
    in lowp vec4 v_color;
    out lowp vec4 color;

    void main(void) {
        color = v_color;
    }
)_";

void init_scene(const int textureWidth, const int textureHeight, bool reinit) {
    if(reinit) {
        init_image_plane(textureWidth, textureHeight);
        return;
    }

    generate_shader(&image_shader_object, ImageVertexShaderGlsl, ImageFragmentShaderGlsl);
    generate_shader(&gui_shader_object, GuiVertexShaderGlsl, GuiFragmentShaderGlsl);
    init_image_plane(textureWidth, textureHeight);
    init_imgui(GUI_WIDTH, GUI_HEIGHT);
    init_texplate();

    create_render_target(&gui_render_target, GUI_WIDTH, GUI_HEIGHT);
}

void init_image_plane(const int textureWidth, const int textureHeight) {

    glGenBuffers(1, &cubeVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Geometry::c_quadVertices), Geometry::c_quadVertices, GL_STATIC_DRAW);

    glGenBuffers(1, &cubeIndexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeIndexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Geometry::c_quadIndices), Geometry::c_quadIndices, GL_STATIC_DRAW);

    vertexAttribCoords = image_shader_object.loc_position;
    vertexAttribTexCoords = image_shader_object.loc_tex_coord;

    glGenVertexArrays(1, &vertexArrayObject);
    glBindVertexArray(vertexArrayObject);
    glEnableVertexAttribArray(vertexAttribCoords);
    glEnableVertexAttribArray(vertexAttribTexCoords);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVertexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeIndexBuffer);
    glVertexAttribPointer(vertexAttribCoords, 3, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex),
                          nullptr);
    glVertexAttribPointer(vertexAttribTexCoords, 2, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex),
                          reinterpret_cast<const void *>(sizeof(XrVector3f)));

    glGenTextures(1, &texture2D);
    glBindTexture(GL_TEXTURE_2D, texture2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB, textureWidth, textureHeight, 0, GL_SRGB, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void render_scene(const XrCompositionLayerProjectionView &layerView,
                  render_target_t &rtarget, const Quad &quad,
                  const std::shared_ptr<AppState> &appState,
                  const CameraFrame *cameraFrame, bool drawGui) {

    glBindFramebuffer(GL_FRAMEBUFFER, rtarget.fbo_id);
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

    glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rtarget.texc_id, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, rtarget.texz_id, 0);

    glClearColor(CLEAR_COLOR[0], CLEAR_COLOR[1], CLEAR_COLOR[2], CLEAR_COLOR[3]);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

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

    draw_image_plane(vp, quad, cameraFrame);
    if (drawGui) draw_imgui(vp, appState);
    
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int draw_image_plane(const XrMatrix4x4f &vp, const Quad &quad, const CameraFrame *cameraFrame) {

    glUseProgram(image_shader_object.program);

    glBindVertexArray(vertexArrayObject);

    auto pos = XrVector3f{quad.Pose.position.x, quad.Pose.position.y, quad.Pose.position.z};
    XrMatrix4x4f model;
    XrMatrix4x4f_CreateTranslationRotationScale(&model, &pos, &quad.Pose.orientation, &quad.Scale);
    XrMatrix4x4f mvp;
    XrMatrix4x4f_Multiply(&mvp, &vp, &model);
    glUniformMatrix4fv(static_cast<GLint>(image_shader_object.loc_mvp), 1, GL_FALSE,
                       reinterpret_cast<const GLfloat *>(&mvp));

    glBindTexture(GL_TEXTURE_2D, texture2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB, cameraFrame->frameWidth, cameraFrame->frameHeight, 0, GL_SRGB, GL_UNSIGNED_BYTE, cameraFrame->dataHandle);

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ArraySize(Geometry::c_quadIndices)),
                   GL_UNSIGNED_SHORT, nullptr);

    glBindVertexArray(0);

    return 0;
}

int draw_imgui(const XrMatrix4x4f &vp, const std::shared_ptr<AppState> &appState) {

    /* save current FBO */
    render_target_t rtarget0{};
    get_render_target(&rtarget0);

    /* render to UIPlane-FBO */
    set_render_target(&gui_render_target);
    glClearColor(1.0f, 0.0f, 1.0f, 0.8f);
    glClear(GL_COLOR_BUFFER_BIT);

    {
        invoke_imgui(appState);
    }

    /* restore FBO */
    set_render_target(&rtarget0);

    glEnable(GL_DEPTH_TEST);

    {
        XrMatrix4x4f matT;
        float win_h = 1.0f;
        float win_w = win_h * ((float) GUI_WIDTH / (float) GUI_HEIGHT);
        XrVector3f translation{1.0f, -0.5f, 0.2f};
        XrQuaternionf rotation{0.0f, 0.0f, 0.0f, 1.0f};
        XrVector3f scale{win_w, win_h, 1.0f};
        XrMatrix4x4f_CreateTranslationRotationScale(&matT, &translation, &rotation, &scale);

        XrMatrix4x4f matPVM;
        XrMatrix4x4f_Multiply(&matPVM, &vp, &matT);
        draw_tex_plate(gui_render_target.texc_id, matPVM);
    }

    return 0;
}