//
// Created by standa on 18.05.23.
//
#pragma once

constexpr char VertexShaderGlsl[] =
        R"_(#version 450

    void main() {
       gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
    }
    )_";

constexpr char FragmentShaderGlsl[] =
        R"_(#version 450

    layout (location = 0) out vec4 outColor;

    void main() {
        outColor = vec4(1.0, 1.0, 1.0, 1.0);
    }
    )_";