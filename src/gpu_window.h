#pragma once

struct IGpuWindow {
    virtual ~IGpuWindow() = default;

    virtual bool Init() = 0;

    EGLDisplay m_display;
    EGLConfig m_config;
    EGLContext m_context;
    EGLSurface m_mainSurface;
};

std::shared_ptr<IGpuWindow> CreateGpuWindow();