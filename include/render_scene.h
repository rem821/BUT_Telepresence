//
// Created by stand on 16.08.2024.
//
#pragma once

#include "util_openxr.h"
#include "linear.h"

struct Quad {
    XrPosef Pose;
    XrVector3f Scale;
};

void init_scene();

void generate_shader();

void init_image_plane();

void render_scene(const XrCompositionLayerProjectionView &layerView, render_target_t &rtarget,
                  const Quad &quad, const void *image);

int draw_image_plane(const XrMatrix4x4f& vp, const Quad &quad, const void *image);

int draw_imgui(const XrMatrix4x4f& vp);