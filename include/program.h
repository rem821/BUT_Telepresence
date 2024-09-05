#pragma once

#include "util_openxr.h"
#include "util_egl.h"
#include "BS_thread_pool.hpp"
#include "servo_communicator.h"
#include "gstreamer_player.h"
#include "rest_client.h"
#include "ntp_timer.h"

#define HANDL_IN    "/user/hand/left/input"
#define HANDR_IN    "/user/hand/right/input"

class TelepresenceProgram {

public:
    TelepresenceProgram(struct android_app *app);

    ~TelepresenceProgram();

    void UpdateFrame();

private:
    void InitializeActions();

    void PollActions();

    void PollPoses(XrTime predictedDisplayTime);

    void RenderFrame();

    bool RenderLayer(XrTime displayTime, std::vector<XrCompositionLayerProjectionView> &layerViews,
                     XrCompositionLayerProjection &layer);

    void SendControllerDatagram();

    void InitializeStreaming();

    XrInstance openxr_instance_ = XR_NULL_HANDLE;
    XrSystemId openxr_system_id_ = XR_NULL_SYSTEM_ID;
    XrSession openxr_session_ = XR_NULL_HANDLE;

    std::vector<viewsurface_t> viewsurfaces_;

    std::vector<XrSpace> reference_spaces_;
    XrSpace app_reference_space_;

    InputState input_;
    UserState userState_;

    bool mono_ = false;
    int32_t speed_ = 200000;


    //int udpSocket_{-1};

    BS::thread_pool threadPool_;

    std::unique_ptr<GstreamerPlayer> gstreamerPlayer_;
    std::unique_ptr<RestClient> restClient_;
    std::unique_ptr<ServoCommunicator> servoCommunicator_;
    std::unique_ptr<NtpTimer> ntpTimer_;
    std::string ntpServerAddress_ = "192.168.1.100";

    unsigned char* testFrame_;
    std::chrono::time_point<std::chrono::high_resolution_clock> prevFrameStart_, frameStart_;

    std::shared_ptr<AppState> appState_{};
};