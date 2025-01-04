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

static int numberOfElements = 5;
static int numberOfSegments = 4;

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
//    win_y += win_h;
//    win_h = 140;
//    ImGui::SetNextWindowPos(ImVec2(_X(win_x), _Y(win_y)), ImGuiCond_FirstUseEver);
//    ImGui::SetNextWindowSize(ImVec2(_X(win_w), _Y(win_h)), ImGuiCond_FirstUseEver);
//    ImGui::Begin("Runtime");
//    {
//        ImGui::Text("OXR_RUNTIME: %s", appState->systemInfo.openXrRuntime.c_str());
//        ImGui::Text("OXR_SYSTEM : %s", appState->systemInfo.openXrSystem.c_str());
//        ImGui::Text("GL_VERSION : %s", appState->systemInfo.openGlVersion);
//        ImGui::Text("GL_VENDOR  : %s", appState->systemInfo.openGlVendor);
//        ImGui::Text("GL_RENDERER: %s", appState->systemInfo.openGlRenderer);
//        ImGui::Text("Framerate: %f, Frametime: %f", appState->appFrameRate,
//                    appState->appFrameTime / 1000.0f);
//        s_win_pos[s_win_num] = ImGui::GetWindowPos();
//        s_win_size[s_win_num] = ImGui::GetWindowSize();
//        s_win_num++;
//    }
//    ImGui::End();

    win_y += win_h;
    win_h = 360;
    ImGui::SetNextWindowPos(ImVec2(_X(win_x), _Y(win_y)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(_X(win_w), _Y(win_h)), ImGuiCond_FirstUseEver);
    ImGui::Begin("Streaming info");
    {

        if (appState->guiControl.changesEnqueued) {
            if (appState->guiControl.focusMoveUp) {
                appState->guiControl.focusedElement -= 1;
                appState->guiControl.focusedSegment = 0;
                if (appState->guiControl.focusedElement < 0) { appState->guiControl.focusedElement = numberOfElements - 1; }
            }
            if (appState->guiControl.focusMoveDown) {
                appState->guiControl.focusedElement += 1;
                appState->guiControl.focusedSegment = 0;
                if (appState->guiControl.focusedElement >= numberOfElements) { appState->guiControl.focusedElement = 0; }
            }
            if (appState->guiControl.focusMoveLeft) {
                appState->guiControl.focusedSegment -= 1;
                if (appState->guiControl.focusedSegment < 0) { appState->guiControl.focusedSegment = numberOfSegments - 1; }
            }
            if (appState->guiControl.focusMoveRight) {
                appState->guiControl.focusedSegment += 1;
                if (appState->guiControl.focusedSegment >= numberOfSegments) { appState->guiControl.focusedSegment = 0; }
            }
            appState->guiControl.focusMoveUp = false;
            appState->guiControl.focusMoveDown = false;
            appState->guiControl.focusMoveLeft = false;
            appState->guiControl.focusMoveRight = false;
            appState->guiControl.cooldown = 25 ;
            appState->guiControl.changesEnqueued = false;
        }

        focusable_text_ip(
                fmt::format("Headset IP: {}", IpToString(appState->streamingConfig.headset_ip)),
                appState->guiControl.focusedElement == 0,
                appState->guiControl.focusedSegment
        );
        focusable_text_ip(
                fmt::format("Camera stream IP: {}", IpToString(appState->streamingConfig.jetson_ip)),
                appState->guiControl.focusedElement == 1,
                appState->guiControl.focusedSegment
        );
        focusable_text(
                fmt::format("Codec: {}", CodecToString(appState->streamingConfig.codec)),
                appState->guiControl.focusedElement == 2
        );
        focusable_text(
                fmt::format("Encoding quality: {}", appState->streamingConfig.encodingQuality),
                appState->guiControl.focusedElement == 3
        );
        focusable_text(
                fmt::format("{}",
                            VideoModeToString(appState->streamingConfig.videoMode)),
                appState->guiControl.focusedElement == 4
        );
        focusable_text(
                fmt::format("Resolution: {}x{} @ {} FPS",
                            appState->streamingConfig.horizontalResolution,
                            appState->streamingConfig.verticalResolution,
                            appState->streamingConfig.fps),
                appState->guiControl.focusedElement == -1 // Disabled focus
        );

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

void focusable_text(const std::string& text, bool isFocused) {
    ImVec2 p = ImGui::GetCursorScreenPos(); // Top-left position of the current drawing cursor
    ImVec2 textSize = ImGui::CalcTextSize(text.c_str()); // Size of the text

    // Draw a background rectangle if isFocused is true
    if (isFocused) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
                p,
                ImVec2(p.x + textSize.x, p.y + textSize.y),
                IM_COL32(100, 100, 255, 100) // Light blue color with transparency
        );
    }

    // Draw the text
    ImGui::Text("%s", text.c_str());
}

void focusable_text_ip(const std::string& text, bool isFocused, int segment) {
    ImVec2 p = ImGui::GetCursorScreenPos(); // Top-left position of the current drawing cursor

    // Split the text into 4 IP segments
    size_t start = text.find_first_of("0123456789");
    for (int i = 0; i < segment && start != std::string::npos; ++i) {
        start = text.find_first_of("0123456789", text.find_first_not_of("0123456789", start));
    }
    size_t end = text.find_first_not_of("0123456789", start);

    // Draw highlight if focused
    if (isFocused && start != std::string::npos) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 highlightStart = ImVec2(p.x + ImGui::CalcTextSize(text.substr(0, start).c_str()).x, p.y);
        ImVec2 highlightSize = ImGui::CalcTextSize(text.substr(start, end - start).c_str());
        drawList->AddRectFilled(highlightStart, ImVec2(highlightStart.x + highlightSize.x, highlightStart.y + highlightSize.y),
                                IM_COL32(100, 100, 255, 100));
    }

    // Render the full text
    ImGui::Text("%s", text.c_str());
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
