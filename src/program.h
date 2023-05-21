#pragma once

#include "options.h"
#include "vulkan/VulkanGraphicsContext.h"
#include "platform_plugin.h"
#include "BS_thread_pool.hpp"
#include "gstreamer_player.h"
#include "udp_socket.h"
#include "servo_communicator.h"

#include <utility>

class OpenXrProgram {
public:
    struct InputState {
        XrActionSet actionSet{XR_NULL_HANDLE};
        XrAction quitAction{XR_NULL_HANDLE};
        XrAction controllerPoseAction{XR_NULL_HANDLE};
        XrAction thumbstickPoseAction{XR_NULL_HANDLE};
        XrAction thumbstickPressedAction{XR_NULL_HANDLE};
        XrAction thumbstickTouchedAction{XR_NULL_HANDLE};
        XrAction buttonAPressedAction{XR_NULL_HANDLE};
        XrAction buttonATouchedAction{XR_NULL_HANDLE};
        XrAction buttonBPressedAction{XR_NULL_HANDLE};
        XrAction buttonBTouchedAction{XR_NULL_HANDLE};
        XrAction buttonXPressedAction{XR_NULL_HANDLE};
        XrAction buttonXTouchedAction{XR_NULL_HANDLE};
        XrAction buttonYPressedAction{XR_NULL_HANDLE};
        XrAction buttonYTouchedAction{XR_NULL_HANDLE};
        XrAction squeezeValueAction{XR_NULL_HANDLE};
        XrAction triggerValueAction{XR_NULL_HANDLE};
        XrAction triggerTouchedAction{XR_NULL_HANDLE};
    };

    OpenXrProgram(const std::shared_ptr<Options> &options,
                  const std::shared_ptr<IPlatformPlugin> &platformPlugin,
                  const std::shared_ptr<VulkanEngine::VulkanGraphicsContext> &graphicsContext);

    ~OpenXrProgram();

    // Create an Instance and other basic instance-level initialization.
    void CreateInstance();

    // Select a System for the view configuration specified in the Options
    void InitializeSystem();

    // Initialize the graphics device for the selected system.
    void InitializeDevice();

    // Create a Session and other basic session-level initialization.
    void InitializeSession();

    // Create the graphics rendering primitives.
    void InitializeRendering();

    // Create Gstreamer instance and start receiving video
    void InitializeStreaming();

    // Create UDP socket
    void InitializeControllerStream();

    // Send UDP packet
    void SendControllerDatagram();

    // Process any events in the event queue.
    void PollEvents(bool *exitRenderLoop, bool *requestRestart);

    // Manage session lifecycle to track if RenderFrame should be called.
    bool IsSessionRunning() const { return m_sessionRunning; };

    // Manage session state to track if input should be processed.
    bool IsSessionFocused() const { return m_sessionState == XR_SESSION_STATE_FOCUSED; };

    // Sample input actions and generate haptic feedback.
    void PollActions();

    // Create and submit a frame.
    void RenderFrame();

    // Get preferred blend mode based on the view configuration specified in the Options
    XrEnvironmentBlendMode GetPreferredBlendMode() const;

private:

    void LogLayersAndExtensions();

    void LogInstanceInfo();

    void CreateInstanceInternal();

    void LogViewConfigurations();

    void LogEnvironmentBlendMode(XrViewConfigurationType type);

    void LogReferenceSpaces();

    void CreateAction(XrActionSet &actionSet, XrActionType type, const char *actionName, const char *localizedName, int countSubactionPaths,
                      const XrPath *subactionPaths, XrAction *action);

    void InitializeActions();

    void CreateVisualizedSpaces();

    const XrEventDataBaseHeader *TryReadNextEvent();

    void HandleSessionStateChangedEvent(const XrEventDataSessionStateChanged &stateChangedEvent,
                                        bool *exitRenderLoop, bool *requestRestart);

    void LogActionSourceName(XrAction action, const std::string &actionName);

    void PollPoses(XrTime predictedDisplayTime);

    bool RenderLayer(XrTime predictedDisplayTime, std::vector<XrCompositionLayerProjectionView> &projectionLayerViews,
            XrCompositionLayerProjection &layer);

    std::string GetXrVersionString(XrVersion ver) {
        return Fmt("%d.%d.%d", XR_VERSION_MAJOR(ver), XR_VERSION_MINOR(ver), XR_VERSION_PATCH(ver));
    }

    XrPosef Identity() {
        XrPosef t{};
        t.orientation.w = 1;
        return t;
    }

    XrPosef Translation(const XrVector3f &translation) {
        XrPosef t = Identity();
        t.position = translation;
        return t;
    }

    XrPosef RotateCCWAboutYAxis(float radians, XrVector3f translation) {
        XrPosef t = Identity();
        t.orientation.x = 0.f;
        t.orientation.y = std::sin(radians * 0.5f);
        t.orientation.z = 0.f;
        t.orientation.w = std::cos(radians * 0.5f);
        t.position = translation;
        return t;
    }

    XrReferenceSpaceCreateInfo GetXrReferenceSpaceCreateInfo(const std::string &referenceSpaceTypeStr);


    const std::shared_ptr<const Options> options_;
    std::shared_ptr<IPlatformPlugin> platformPlugin_;
    std::shared_ptr<VulkanEngine::VulkanGraphicsContext> graphicsContext_;

    BS::thread_pool threadPool;
    GstreamerPlayer gstreamerPlayer{threadPool};

    XrSpace m_appSpace{XR_NULL_HANDLE};
    XrInstance m_instance{XR_NULL_HANDLE};
    XrSystemId m_systemId{XR_NULL_SYSTEM_ID};
    XrSession m_session{XR_NULL_HANDLE};

    XrEnvironmentBlendMode m_preferredBlendMode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};

    std::map<XrSwapchain, std::vector<XrSwapchainImageBaseHeader *>> m_swapchainImages;
    std::vector<XrView> m_views;

    std::vector<XrSpace> m_visualizedSpaces;
    XrSpace controllerSpace[Side::COUNT];

    XrSessionState m_sessionState{XR_SESSION_STATE_UNKNOWN};
    bool m_sessionRunning{false};

    XrPath handSubactionPath[Side::COUNT];

    XrEventDataBuffer m_eventDataBuffer{};
    InputState m_input;

    const std::set<XrEnvironmentBlendMode> m_acceptableBlendModes;

    UserState userState{};

    int udpSocket;
    ServoCommunicator servoComm{threadPool};
};

struct Swapchain {
    XrSwapchain handle;
    int32_t width;
    int32_t height;
};