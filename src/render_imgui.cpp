/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2020 terryky1220@gmail.com
 * ------------------------------------------------ */
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "render_imgui.h"
#include "openxr/openxr.h"

#define DISPLAY_SCALE_X 1
#define DISPLAY_SCALE_Y 1
#define _X(x)       ((float)(x) / DISPLAY_SCALE_X)
#define _Y(y)       ((float)(y) / DISPLAY_SCALE_Y)

static int s_win_w;
static int s_win_h;

static ImVec2 s_win_size[10];
static ImVec2 s_win_pos[10];
static int s_win_num = 0;
static ImVec2 s_mouse_pos;

int
init_imgui(int win_w, int win_h) {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer bindings
    ImGui_ImplOpenGL3_Init(NULL);

    io.DisplaySize = ImVec2(_X(win_w), _Y(win_h));
    io.DisplayFramebufferScale = {DISPLAY_SCALE_X, DISPLAY_SCALE_Y};

    s_win_w = win_w;
    s_win_h = win_h;

    return 0;
}

void
imgui_mousebutton(int button, int state, int x, int y) {
    ImGuiIO &io = ImGui::GetIO();
    io.MousePos = ImVec2(_X(x), (float) _Y(y));

    if (state)
        io.MouseDown[button] = true;
    else
        io.MouseDown[button] = false;

    s_mouse_pos.x = x;
    s_mouse_pos.y = y;
}

void
imgui_mousemove(int x, int y) {
    ImGuiIO &io = ImGui::GetIO();
    io.MousePos = ImVec2(_X(x), _Y(y));

    s_mouse_pos.x = x;
    s_mouse_pos.y = y;
}

bool
imgui_is_anywindow_hovered() {
#if 1
    int x = _X(s_mouse_pos.x);
    int y = _Y(s_mouse_pos.y);
    for (int i = 0; i < s_win_num; i++) {
        int x0 = s_win_pos[i].x;
        int y0 = s_win_pos[i].y;
        int x1 = x0 + s_win_size[i].x;
        int y1 = y0 + s_win_size[i].y;
        if ((x >= x0) && (x < x1) && (y >= y0) && (y < y1))
            return true;
    }
    return false;
#else
    return ImGui::IsAnyWindowHovered();
#endif
}

static void render_gui(const std::shared_ptr<AppState> &appState) {
    int win_w = 300;
    int win_h = 0;
    int win_x = 0;
    int win_y = 0;

    s_win_num = 0;

    /* Show main window */
    win_y += win_h;
    win_h = 140;
    ImGui::SetNextWindowPos(ImVec2(_X(win_x), _Y(win_y)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(_X(win_w), _Y(win_h)), ImGuiCond_FirstUseEver);
    ImGui::Begin("Runtime");
    {
        ImGui::Text("OXR_RUNTIME: %s", appState->systemInfo.openXrRuntime.c_str());
        ImGui::Text("OXR_SYSTEM : %s", appState->systemInfo.openXrSystem.c_str());
        ImGui::Text("GL_VERSION : %s", appState->systemInfo.openGlVersion);
        ImGui::Text("GL_VENDOR  : %s", appState->systemInfo.openGlVendor);
        ImGui::Text("GL_RENDERER: %s", appState->systemInfo.openGlRenderer);
        ImGui::Text("Framerate: %f, Frametime: %f", appState->appFrameRate, appState->appFrameTime / 1000.0f);
        s_win_pos[s_win_num] = ImGui::GetWindowPos();
        s_win_size[s_win_num] = ImGui::GetWindowSize();
        s_win_num++;
    }
    ImGui::End();

    win_y += win_h;
    win_h = 220;
    ImGui::SetNextWindowPos(ImVec2(_X(win_x), _Y(win_y)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(_X(win_w), _Y(win_h)), ImGuiCond_FirstUseEver);
    ImGui::Begin("Streaming info");
    {
        ImGui::Text("Destination IP: %s [%d:%d]", appState->streamingConfig.ip.c_str(),
                    appState->streamingConfig.portLeft, appState->streamingConfig.portRight);
        ImGui::Text("Codec: %d @ Encoding quality: %d", appState->streamingConfig.codec,
                    appState->streamingConfig.encodingQuality);
        ImGui::Text("Resolution: %dx%d @ %d FPS", appState->streamingConfig.horizontalResolution,
                    appState->streamingConfig.verticalResolution, appState->streamingConfig.fps);
        if (appState->streamingConfig.videoMode == VideoMode::STEREO) {
            ImGui::Text("Stereo");
        } else {
            ImGui::Text("Mono");
        }
        ImGui::Text("");
        ImGui::Text("Latencies:");
        auto s = appState->cameraStreamingStates.first.stats;
        ImGui::Text(
                "NvvidConv: %lu, JpegEnc: %lu, RtpJpegPay: %lu\nUdpStream: %lu\nRtpJpegDepay: %lu, JpegDec: %lu, Queue: %lu",
                s->nvvidconv / 1000, s->jpegenc / 1000, s->rtpjpegpay / 1000, s->udpstream / 1000,
                s->rtpjpegdepay / 1000, s->jpegdec / 1000, s->queue / 1000);

        s_win_pos[s_win_num] = ImGui::GetWindowPos();
        s_win_size[s_win_num] = ImGui::GetWindowSize();
        s_win_num++;
    }
    ImGui::End();
}

int
invoke_imgui(const std::shared_ptr<AppState> &appState) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    render_gui(appState);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return 0;
}
