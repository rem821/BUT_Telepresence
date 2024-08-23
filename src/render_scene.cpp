//
// Created by stand on 16.08.2024.
//
#include "pch.h"
#include <GLES3/gl3.h>
#include "util_shader.h"
#include "geometry.h"
#include "linear.h"
#include <random>

#include "render_scene.h"

static int TEXTURE_WIDTH = 1920;
static int TEXTURE_HEIGHT = 1080;
static const std::array<float, 4> CLEAR_COLOR{0.01f, 0.01f, 0.01f, 1.0f};

static GLuint cubeVertexBuffer{0}, cubeIndexBuffer{0}, vertexArrayObject{0},
        vertexAttribCoords{0}, vertexAttribTexCoords{0}, texture2D{0};

static shader_obj_t shader_object;

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

void init_scene() {
    generate_shader(&shader_object, VertexShaderGlsl, FragmentShaderGlsl);
    init_image_plane();
}

void init_image_plane() {

    glGenBuffers(1, &cubeVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Geometry::c_quadVertices), Geometry::c_quadVertices,
                 GL_STATIC_DRAW);

    glGenBuffers(1, &cubeIndexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeIndexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Geometry::c_quadIndices), Geometry::c_quadIndices,
                 GL_STATIC_DRAW);

    vertexAttribCoords = shader_object.loc_position;
    vertexAttribTexCoords = shader_object.loc_tex_coord;

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

    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB, TEXTURE_WIDTH, TEXTURE_HEIGHT, 0, GL_SRGB,
                 GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void render_scene(const XrCompositionLayerProjectionView &layerView,
                  render_target_t &rtarget, const Quad &quad, const void *image) {

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

    glUseProgram(shader_object.program);

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

    glBindVertexArray(vertexArrayObject);

    auto pos = XrVector3f{quad.Pose.position.x, quad.Pose.position.y, quad.Pose.position.z};
    XrMatrix4x4f model;
    XrMatrix4x4f_CreateTranslationRotationScale(&model, &pos, &quad.Pose.orientation, &quad.Scale);
    XrMatrix4x4f mvp;
    XrMatrix4x4f_Multiply(&mvp, &vp, &model);
    glUniformMatrix4fv(static_cast<GLint>(shader_object.loc_mvp), 1, GL_FALSE, reinterpret_cast<const GLfloat *>(&mvp));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ArraySize(Geometry::c_quadIndices)), GL_UNSIGNED_SHORT, nullptr);

    glBindTexture(GL_TEXTURE_2D, texture2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB, TEXTURE_WIDTH, TEXTURE_HEIGHT, 0, GL_SRGB, GL_UNSIGNED_BYTE, image);

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int draw_image_plane() {

    return 0;
}