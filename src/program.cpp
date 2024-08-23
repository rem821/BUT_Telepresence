
#include "pch.h"
#include "log.h"
#include "check.h"
#include "udp_socket.h"
#include "render_scene.h"
#include "render_imgui.h"

#include <utility>
#include <GLES3/gl32.h>

#include "program.h"

TelepresenceProgram::TelepresenceProgram(struct android_app *app) {

    // Initialize the OpenXR loader which detects, picks and interfaces with an OpenXR runtime running on the target device
    openxr_init_loader(app);

    openxr_create_instance(app, &openxr_instance_);

    openxr_get_system_id(&openxr_instance_, &openxr_system_id_);

    egl_init_with_pbuffer_surface();
    openxr_confirm_gfx_reqs(&openxr_instance_, &openxr_system_id_);

    init_scene();

    openxr_create_session(&openxr_instance_, &openxr_system_id_, &openxr_session_);
    openxr_log_reference_spaces(&openxr_session_);
    openxr_create_reference_spaces(&openxr_session_, reference_spaces_);
    app_reference_space_ = reference_spaces_[0]; // "ViewFront"

    viewsurfaces_ = openxr_create_swapchains(&openxr_instance_, &openxr_system_id_,
                                             &openxr_session_);

    testFrame_ = new unsigned char[1920*1080*3];
    for (int i = 0; i < 1920 * 1080 * 3; ++i) {
        testFrame_[i] = rand() % 255;  // Generate a random number between 0 and 254
    }
    InitializeActions();
    InitializeStreaming();
}

void TelepresenceProgram::UpdateFrame() {
    bool exit, request_restart;
    openxr_poll_events(&openxr_instance_, &openxr_session_, &exit, &request_restart);

    if (!openxr_is_session_running()) {
        return;
    }

    PollActions();
    //TODO: SendControllerDatagram();

    RenderFrame();
}

void TelepresenceProgram::RenderFrame() {
    XrTime display_time, elapsed_us;
    openxr_begin_frame(&openxr_session_, &display_time);

    PollPoses(display_time);

    std::vector<XrCompositionLayerBaseHeader *> layers;
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::vector<XrCompositionLayerProjectionView> projectionLayerViews;
    if (RenderLayer(display_time, projectionLayerViews, layer)) {
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer));
    }

    openxr_end_frame(&openxr_session_, &display_time, layers);
}

bool TelepresenceProgram::RenderLayer(XrTime displayTime,
                                      std::vector<XrCompositionLayerProjectionView> &layerViews,
                                      XrCompositionLayerProjection &layer) {
    //TODO: displayTime += 50e6; //Predict 50 ms into the future
    auto viewCount = viewsurfaces_.size();
    std::vector<XrView> views(viewCount, {XR_TYPE_VIEW});
    openxr_locate_views(&openxr_session_, &displayTime, app_reference_space_, viewCount,
                        views.data());

    layerViews.resize(viewCount);

    XrSpaceLocation spaceLocation{XR_TYPE_SPACE_LOCATION};

    // Locate "Local" space relative to "ViewFront"
    auto res = xrLocateSpace(reference_spaces_[1], app_reference_space_, displayTime,
                             &spaceLocation);
    CHECK_XRRESULT(res, "xrLocateSpace")
    if (XR_UNQUALIFIED_SUCCESS(res)) {
        if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
            (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {

            userState_.hmdPose = spaceLocation.pose;
        }
    } else {
        LOG_INFO("Unable to locate a visualized reference space in app space: %d", res);
    }

    Quad quad{};
    quad.Pose.position = {0.0f, 0.0f, 0.0f};
    quad.Pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    quad.Scale = {4.6f, 2.6f, 0.0f};

    for (uint32_t i = 0; i < viewCount; i++) {
        XrSwapchainSubImage subImg;
        render_target_t rtarget;

        openxr_acquire_viewsurface(viewsurfaces_[i], rtarget, subImg);

        layerViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        layerViews[i].pose = views[i].pose;
        layerViews[i].fov = views[i].fov;
        layerViews[i].subImage = subImg;

        void *imageHandle = i == 0 ? gstreamerPlayer_.getFrameRight().dataHandle
                                   : gstreamerPlayer_.getFrameLeft().dataHandle;
        if (userState_.aPressed) mono_ = true;
        if (userState_.bPressed) mono_ = false;
        if (mono_) imageHandle = gstreamerPlayer_.getFrameRight().dataHandle;

        auto start = std::chrono::high_resolution_clock::now();

        render_scene(layerViews[i], rtarget, quad, imageHandle);

        openxr_release_viewsurface(viewsurfaces_[i]);
        auto end = std::chrono::high_resolution_clock::now();
//        LOG_ERROR("Timing: %lld ",
//                  std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    }

    layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = app_reference_space_;
    layer.layerFlags = 0;
    layer.viewCount = layerViews.size();
    layer.views = layerViews.data();

    return true;
}

void TelepresenceProgram::InitializeActions() {
    input_.actionSet = openxr_create_actionset(&openxr_instance_, "gameplay", "Gameplay", 0);

    CHECK_XRCMD(xrStringToPath(openxr_instance_, "/user/hand/right",
                               &input_.handSubactionPath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(openxr_instance_, "/user/hand/left",
                               &input_.handSubactionPath[Side::LEFT]))


    input_.quitAction = openxr_create_action(&input_.actionSet,
                                             XR_ACTION_TYPE_BOOLEAN_INPUT,
                                             "quit_session",
                                             "Quit Session",
                                             0,
                                             nullptr);

    input_.controllerPoseAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_POSE_INPUT,
                                                       "controller_pose",
                                                       "Controller Pose",
                                                       Side::COUNT,
                                                       input_.handSubactionPath.data());

    input_.thumbstickPoseAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_VECTOR2F_INPUT,
                                                       "thumbstick_pose",
                                                       "Thumbstick Pose",
                                                       Side::COUNT,
                                                       input_.handSubactionPath.data());

    input_.thumbstickPressedAction = openxr_create_action(&input_.actionSet,
                                                          XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                          "thumbstick_pressed",
                                                          "Thumbstick Pressed",
                                                          Side::COUNT,
                                                          input_.handSubactionPath.data());

    input_.thumbstickTouchedAction = openxr_create_action(&input_.actionSet,
                                                          XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                          "thumbstick_touched",
                                                          "Thumbstick Touched",
                                                          Side::COUNT,
                                                          input_.handSubactionPath.data());

    input_.buttonAPressedAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                       "button_a_pressed",
                                                       "Button A Pressed",
                                                       1,
                                                       &input_.handSubactionPath[Side::RIGHT]);

    input_.buttonATouchedAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                       "button_a_touched",
                                                       "Button A Touched",
                                                       1,
                                                       &input_.handSubactionPath[Side::RIGHT]);

    input_.buttonBPressedAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                       "button_b_pressed",
                                                       "Button B Pressed",
                                                       1,
                                                       &input_.handSubactionPath[Side::RIGHT]);

    input_.buttonBTouchedAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                       "button_b_touched",
                                                       "Button B Touched",
                                                       1,
                                                       &input_.handSubactionPath[Side::RIGHT]);

    input_.buttonXPressedAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                       "button_x_pressed",
                                                       "Button X Pressed",
                                                       1,
                                                       &input_.handSubactionPath[Side::LEFT]);

    input_.buttonXTouchedAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                       "button_x_touched",
                                                       "Button X Touched",
                                                       1,
                                                       &input_.handSubactionPath[Side::LEFT]);

    input_.buttonYPressedAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                       "button_y_pressed",
                                                       "Button Y Pressed",
                                                       1,
                                                       &input_.handSubactionPath[Side::LEFT]);

    input_.buttonYTouchedAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                       "button_y_touched",
                                                       "Button Y Touched",
                                                       1,
                                                       &input_.handSubactionPath[Side::LEFT]);

    input_.squeezeValueAction = openxr_create_action(&input_.actionSet,
                                                     XR_ACTION_TYPE_FLOAT_INPUT,
                                                     "squeeze_value",
                                                     "Squeeze Value",
                                                     Side::COUNT,
                                                     input_.handSubactionPath.data());

    input_.triggerValueAction = openxr_create_action(&input_.actionSet,
                                                     XR_ACTION_TYPE_FLOAT_INPUT,
                                                     "trigger_value",
                                                     "Trigger Value",
                                                     Side::COUNT,
                                                     input_.handSubactionPath.data());

    input_.triggerTouchedAction = openxr_create_action(&input_.actionSet,
                                                       XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                       "trigger_touched",
                                                       "Trigger Touched",
                                                       Side::COUNT,
                                                       input_.handSubactionPath.data());

    std::vector<XrActionSuggestedBinding> bindings;
    bindings.push_back(
            {input_.quitAction, openxr_string2path(&openxr_instance_, HANDL_IN"/menu/click")});
    bindings.push_back(
            {input_.quitAction, openxr_string2path(&openxr_instance_, HANDR_IN"/menu/click")});
    openxr_bind_interaction(&openxr_instance_, "/interaction_profiles/khr/simple_controller",
                            bindings);

    std::vector<XrActionSuggestedBinding> touch_bindings;
    touch_bindings.push_back(
            {input_.quitAction, openxr_string2path(&openxr_instance_, HANDL_IN"/menu/click")});
    touch_bindings.push_back({input_.controllerPoseAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/aim/pose")});
    touch_bindings.push_back({input_.controllerPoseAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/aim/pose")});
    touch_bindings.push_back({input_.thumbstickPoseAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/thumbstick")});
    touch_bindings.push_back({input_.thumbstickPoseAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/thumbstick")});
    touch_bindings.push_back({input_.thumbstickPressedAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/thumbstick/click")});
    touch_bindings.push_back({input_.thumbstickPressedAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/thumbstick/click")});
    touch_bindings.push_back({input_.thumbstickTouchedAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/thumbstick/touch")});
    touch_bindings.push_back({input_.thumbstickTouchedAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/thumbstick/touch")});
    touch_bindings.push_back({input_.buttonAPressedAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/a/click")});
    touch_bindings.push_back({input_.buttonATouchedAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/a/touch")});
    touch_bindings.push_back({input_.buttonBPressedAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/b/click")});
    touch_bindings.push_back({input_.buttonBTouchedAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/b/touch")});
    touch_bindings.push_back({input_.buttonXPressedAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/x/click")});
    touch_bindings.push_back({input_.buttonXTouchedAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/x/touch")});
    touch_bindings.push_back({input_.buttonYPressedAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/y/click")});
    touch_bindings.push_back({input_.buttonYTouchedAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/y/touch")});
    touch_bindings.push_back({input_.squeezeValueAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/squeeze/value")});
    touch_bindings.push_back({input_.squeezeValueAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/squeeze/value")});
    touch_bindings.push_back({input_.triggerValueAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/trigger/value")});
    touch_bindings.push_back({input_.triggerValueAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/trigger/value")});
    touch_bindings.push_back({input_.triggerTouchedAction,
                              openxr_string2path(&openxr_instance_, HANDL_IN"/trigger/touch")});
    touch_bindings.push_back({input_.triggerTouchedAction,
                              openxr_string2path(&openxr_instance_, HANDR_IN"/trigger/touch")});
    openxr_bind_interaction(&openxr_instance_, "/interaction_profiles/oculus/touch_controller",
                            touch_bindings);

    openxr_attach_actionset(&openxr_session_, input_.actionSet);

    input_.controllerSpace[Side::LEFT] = openxr_create_action_space(&openxr_session_,
                                                                    input_.controllerPoseAction,
                                                                    input_.handSubactionPath[Side::LEFT]);
    input_.controllerSpace[Side::RIGHT] = openxr_create_action_space(&openxr_session_,
                                                                     input_.controllerPoseAction,
                                                                     input_.handSubactionPath[Side::RIGHT]);

}

void TelepresenceProgram::PollPoses(XrTime predictedDisplayTime) {
    const XrActiveActionSet activeActionSet{input_.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    CHECK_XRCMD(xrSyncActions(openxr_session_, &syncInfo))

    // Controller poses
    for (int i = 0; i < Side::COUNT; i++) {
        XrSpaceVelocity vel = {XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
        loc.next = &vel;

        CHECK_XRCMD(xrLocateSpace(input_.controllerSpace[i], app_reference_space_, predictedDisplayTime, &loc))
        if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
            userState_.controllerPose[i] = loc.pose;
        }
    }
}

void TelepresenceProgram::PollActions() {
    const XrActiveActionSet activeActionSet{input_.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    CHECK_XRCMD(xrSyncActions(openxr_session_, &syncInfo))

    XrActionStateGetInfo getQuitInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, input_.quitAction,
                                     XR_NULL_PATH};
    XrActionStateBoolean quitValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getQuitInfo, &quitValue))
    if ((quitValue.isActive == XR_TRUE) && (quitValue.changedSinceLastSync == XR_TRUE) &&
        (quitValue.currentState == XR_TRUE)) {
        CHECK_XRCMD(xrRequestExitSession(openxr_session_))
    }

    // Thumbstick pose
    XrActionStateGetInfo getThumbstickPoseRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                    input_.thumbstickPoseAction,
                                                    input_.handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getThumbstickPoseLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                   input_.thumbstickPoseAction,
                                                   input_.handSubactionPath[Side::LEFT]};
    XrActionStateVector2f thumbstickPose{XR_TYPE_ACTION_STATE_VECTOR2F};

    CHECK_XRCMD(
            xrGetActionStateVector2f(openxr_session_, &getThumbstickPoseRightInfo, &thumbstickPose))
    userState_.thumbstickPose[Side::RIGHT] = thumbstickPose.currentState;

    CHECK_XRCMD(
            xrGetActionStateVector2f(openxr_session_, &getThumbstickPoseLeftInfo, &thumbstickPose))
    userState_.thumbstickPose[Side::LEFT] = thumbstickPose.currentState;

    // Thumbstick pressed
    XrActionStateGetInfo getThumbstickPressedRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                       input_.thumbstickPressedAction,
                                                       input_.handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getThumbstickPressedLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                      input_.thumbstickPressedAction,
                                                      input_.handSubactionPath[Side::LEFT]};
    XrActionStateBoolean thumbstickPressed{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getThumbstickPressedRightInfo,
                                        &thumbstickPressed))
    userState_.thumbstickPressed[Side::RIGHT] = thumbstickPressed.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getThumbstickPressedLeftInfo,
                                        &thumbstickPressed))
    userState_.thumbstickPressed[Side::LEFT] = thumbstickPressed.currentState;

    // Thumbstick touched
    XrActionStateGetInfo getThumbstickTouchedRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                       input_.thumbstickTouchedAction,
                                                       input_.handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getThumbstickTouchedLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                      input_.thumbstickTouchedAction,
                                                      input_.handSubactionPath[Side::LEFT]};
    XrActionStateBoolean thumbstickTouched{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getThumbstickTouchedRightInfo,
                                        &thumbstickTouched))
    userState_.thumbstickTouched[Side::RIGHT] = thumbstickTouched.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getThumbstickTouchedLeftInfo,
                                        &thumbstickTouched))
    userState_.thumbstickTouched[Side::LEFT] = thumbstickTouched.currentState;

    // Button A
    XrActionStateGetInfo getButtonAPressedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                               input_.buttonAPressedAction, XR_NULL_PATH};
    XrActionStateGetInfo getButtonATouchedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                               input_.buttonATouchedAction, XR_NULL_PATH};
    XrActionStateBoolean buttonA{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getButtonAPressedInfo, &buttonA))
    userState_.aPressed = buttonA.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getButtonATouchedInfo, &buttonA))
    userState_.aTouched = buttonA.currentState;

    // Button B
    XrActionStateGetInfo getButtonBPressedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                               input_.buttonBPressedAction, XR_NULL_PATH};
    XrActionStateGetInfo getButtonBTouchedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                               input_.buttonBTouchedAction, XR_NULL_PATH};
    XrActionStateBoolean buttonB{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getButtonBPressedInfo, &buttonB))
    userState_.bPressed = buttonB.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getButtonBTouchedInfo, &buttonB))
    userState_.bTouched = buttonB.currentState;

    // Button X
    XrActionStateGetInfo getButtonXPressedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                               input_.buttonXPressedAction, XR_NULL_PATH};
    XrActionStateGetInfo getButtonXTouchedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                               input_.buttonXTouchedAction, XR_NULL_PATH};
    XrActionStateBoolean buttonX{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getButtonXPressedInfo, &buttonX))
    userState_.xPressed = buttonX.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getButtonXTouchedInfo, &buttonX))
    userState_.xTouched = buttonX.currentState;

    // Button Y
    XrActionStateGetInfo getButtonYPressedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                               input_.buttonYPressedAction, XR_NULL_PATH};
    XrActionStateGetInfo getButtonYTouchedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                               input_.buttonYTouchedAction, XR_NULL_PATH};
    XrActionStateBoolean buttonY{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getButtonYPressedInfo, &buttonY))
    userState_.yPressed = buttonY.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getButtonYTouchedInfo, &buttonY))
    userState_.yTouched = buttonY.currentState;

    // Squeeze
    XrActionStateGetInfo getSqueezeValueRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                  input_.squeezeValueAction,
                                                  input_.handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getSqueezeValueLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                 input_.squeezeValueAction,
                                                 input_.handSubactionPath[Side::LEFT]};
    XrActionStateFloat squeezeValue{XR_TYPE_ACTION_STATE_FLOAT};

    CHECK_XRCMD(xrGetActionStateFloat(openxr_session_, &getSqueezeValueRightInfo, &squeezeValue))
    userState_.squeezeValue[Side::RIGHT] = squeezeValue.currentState;

    CHECK_XRCMD(xrGetActionStateFloat(openxr_session_, &getSqueezeValueLeftInfo, &squeezeValue))
    userState_.squeezeValue[Side::LEFT] = squeezeValue.currentState;

    // Trigger value
    XrActionStateGetInfo getTriggerValueRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                  input_.triggerValueAction,
                                                  input_.handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getTriggerValueLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                 input_.triggerValueAction,
                                                 input_.handSubactionPath[Side::LEFT]};
    XrActionStateFloat triggerValue{XR_TYPE_ACTION_STATE_FLOAT};

    CHECK_XRCMD(xrGetActionStateFloat(openxr_session_, &getTriggerValueRightInfo, &triggerValue))
    userState_.triggerValue[Side::RIGHT] = triggerValue.currentState;

    CHECK_XRCMD(xrGetActionStateFloat(openxr_session_, &getTriggerValueLeftInfo, &triggerValue))
    userState_.triggerValue[Side::LEFT] = triggerValue.currentState;

    // Trigger touched
    XrActionStateGetInfo getTriggerTouchedRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                    input_.triggerTouchedAction,
                                                    input_.handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getTriggerTouchedLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr,
                                                   input_.triggerTouchedAction,
                                                   input_.handSubactionPath[Side::LEFT]};
    XrActionStateBoolean triggerTouched{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(
            xrGetActionStateBoolean(openxr_session_, &getTriggerTouchedRightInfo, &triggerTouched))
    userState_.triggerTouched[Side::RIGHT] = triggerTouched.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(openxr_session_, &getTriggerTouchedLeftInfo, &triggerTouched))
    userState_.triggerTouched[Side::LEFT] = triggerTouched.currentState;
}

//void TelepresenceProgram::SendControllerDatagram() {
//    if(udpSocket_ == -1) udpSocket_ = createSocket();
//
//    //sendUDPPacket(udpSocket_, userState_);
//    if (!servoComm.servosEnabled()) {
//        servoComm.enableServos(true, threadPool_);
//    }
//    if (servoComm.isReady()) {
//        if (userState_.xPressed) { speed_ -= 10000; }
//        if (userState_.yPressed) { speed_ += 10000; }
//        if (userState_.xPressed && userState_.yPressed) { servoComm.resetErrors(threadPool_); }
//
//        servoComm.setPoseAndSpeed(userState_.hmdPose.orientation, speed_, threadPool_);
//    }
//}

void TelepresenceProgram::InitializeStreaming() {
    gstreamerPlayer_.play();
}