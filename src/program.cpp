
#include "program.h"


OpenXrProgram::OpenXrProgram(const std::shared_ptr<Options> &options,
                             const std::shared_ptr<IPlatformPlugin> &platformPlugin,
                             const std::shared_ptr<VulkanEngine::VulkanGraphicsContext> &graphicsContext)
        : options_(options),
          platformPlugin_(std::move(platformPlugin)),
          graphicsContext_(std::move(graphicsContext)),
          m_acceptableBlendModes{XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
                                 XR_ENVIRONMENT_BLEND_MODE_ADDITIVE,
                                 XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND} {}

OpenXrProgram::~OpenXrProgram() {
    if (m_input.actionSet != XR_NULL_HANDLE) {
        for (auto hand: {Side::LEFT, Side::RIGHT}) {
            xrDestroySpace(controllerSpace[hand]);
        }
        xrDestroyActionSet(m_input.actionSet);
    }

    for (XrSpace visualizedSpace: m_visualizedSpaces) {
        xrDestroySpace(visualizedSpace);
    }

    if (m_appSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_appSpace);
    }

    if (m_session != XR_NULL_HANDLE) {
        xrDestroySession(m_session);
    }

    if (m_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(m_instance);
    }
};

void OpenXrProgram::LogLayersAndExtensions() {
    const auto logExtensions = [](const char *layerName, int indent = 0) {
        uint32_t instanceExtensionCount;
        CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(layerName, 0, &instanceExtensionCount, nullptr))

        std::vector<XrExtensionProperties> extensions(instanceExtensionCount);
        for (XrExtensionProperties &extension: extensions) {
            extension.type = XR_TYPE_EXTENSION_PROPERTIES;
        }

        CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(layerName, (uint32_t) extensions.size(),
                                                           &instanceExtensionCount,
                                                           extensions.data()))

        const std::string indentStr(indent, ' ');
        LOG_INFO("%sAvailable Extensions: (%d)", indentStr.c_str(), instanceExtensionCount);
        for (const XrExtensionProperties &extension: extensions) {
            LOG_INFO("%s  Name=%s SpecVersion=%d", indentStr.c_str(), extension.extensionName, extension.extensionVersion);
        }
    };

    // Log non-layer extensions (layerName==nullptr).
    logExtensions(nullptr);

    // Log layers and any of their extensions.
    {
        uint32_t layerCount;
        CHECK_XRCMD(xrEnumerateApiLayerProperties(0, &layerCount, nullptr))

        std::vector<XrApiLayerProperties> layers(layerCount);
        for (XrApiLayerProperties &layer: layers) {
            layer.type = XR_TYPE_API_LAYER_PROPERTIES;
        }

        CHECK_XRCMD(xrEnumerateApiLayerProperties((uint32_t) layers.size(), &layerCount, layers.data()))

        LOG_INFO("Available Layers: (%d)", layerCount);
        for (const XrApiLayerProperties &layer: layers) {
            LOG_INFO("  Name=%s SpecVersion=%s LayerVersion=%d Description=%s", layer.layerName,
                     GetXrVersionString(layer.specVersion).c_str(), layer.layerVersion, layer.description);
            logExtensions(layer.layerName, 4);
        }
    }
}

void OpenXrProgram::LogInstanceInfo() {
    CHECK(m_instance != XR_NULL_HANDLE)

    XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
    CHECK_XRCMD(xrGetInstanceProperties(m_instance, &instanceProperties))

    LOG_INFO("Instance RuntimeName=%s RuntimeVersion=%s", instanceProperties.runtimeName,
             GetXrVersionString(instanceProperties.runtimeVersion).c_str());
}

void OpenXrProgram::CreateInstanceInternal() {
    CHECK(m_instance == XR_NULL_HANDLE)

    // Create union of extensions required by platform and graphics plugins.
    std::vector<const char *> extensions;

    // Transform platform and graphics extension std::strings to C strings.
    const std::vector<std::string> platformExtensions = platformPlugin_->GetInstanceExtensions();
    std::transform(platformExtensions.begin(), platformExtensions.end(),
                   std::back_inserter(extensions),
                   [](const std::string &ext) { return ext.c_str(); });
    const std::vector<std::string> graphicsExtensions = graphicsContext_->GetInstanceExtensions();
    std::transform(graphicsExtensions.begin(), graphicsExtensions.end(),
                   std::back_inserter(extensions),
                   [](const std::string &ext) { return ext.c_str(); });

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = platformPlugin_->GetInstanceCreateExtension();
    createInfo.enabledExtensionCount = (uint32_t) extensions.size();
    createInfo.enabledExtensionNames = extensions.data();

    strcpy(createInfo.applicationInfo.applicationName, "BUT_Telepresence");
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    CHECK_XRCMD(xrCreateInstance(&createInfo, &m_instance))
}

void OpenXrProgram::CreateInstance() {
    LogLayersAndExtensions();

    CreateInstanceInternal();

    LogInstanceInfo();
}

void OpenXrProgram::LogViewConfigurations() {
    CHECK(m_instance != XR_NULL_HANDLE)
    CHECK(m_systemId != XR_NULL_SYSTEM_ID)

    uint32_t viewConfigTypeCount;
    CHECK_XRCMD(xrEnumerateViewConfigurations(m_instance, m_systemId, 0, &viewConfigTypeCount,
                                              nullptr))
    std::vector<XrViewConfigurationType> viewConfigTypes(viewConfigTypeCount);
    CHECK_XRCMD(xrEnumerateViewConfigurations(m_instance, m_systemId, viewConfigTypeCount,
                                              &viewConfigTypeCount,
                                              viewConfigTypes.data()))
    CHECK((uint32_t) viewConfigTypes.size() == viewConfigTypeCount)

    LOG_INFO("Available View Configuration Types: (%d)", viewConfigTypeCount);
    for (auto viewConfigType: viewConfigTypes) {
        LOG_INFO("  View Configuration Type: %s %s", to_string(viewConfigType),
                 viewConfigType == options_->Parsed.ViewConfigType ? "(Selected)" : "");

        XrViewConfigurationProperties viewConfigProperties{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
        CHECK_XRCMD(xrGetViewConfigurationProperties(m_instance, m_systemId, viewConfigType,
                                                     &viewConfigProperties))

        LOG_INFO("  View Configuration FovMutable=%s", viewConfigProperties.fovMutable == XR_TRUE ? "True" : "False");

        uint32_t viewCount;
        CHECK_XRCMD(xrEnumerateViewConfigurationViews(m_instance, m_systemId, viewConfigType, 0,
                                                      &viewCount,
                                                      nullptr))
        if (viewCount > 0) {
            std::vector<XrViewConfigurationView> views(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
            CHECK_XRCMD(
                    xrEnumerateViewConfigurationViews(m_instance, m_systemId, viewConfigType,
                                                      viewCount,
                                                      &viewCount,
                                                      views.data()))

            for (uint32_t i = 0; i < views.size(); i++) {
                const XrViewConfigurationView &view = views[i];
                LOG_INFO("    View [%d]: Recommended Width=%d Height=%d SampleCount=%d", i,
                         view.recommendedImageRectWidth, view.recommendedImageRectHeight,
                         view.recommendedSwapchainSampleCount);
                LOG_INFO("    View [%d]:   Maximum Width=%d Height=%d SampleCount=%d", i,
                         view.maxImageRectWidth, view.maxImageRectHeight,
                         view.maxSwapchainSampleCount);
            }
        } else {
            LOG_ERROR("Empty view configuration type");
        }

        LogEnvironmentBlendMode(viewConfigType);
    }
}

void OpenXrProgram::LogEnvironmentBlendMode(XrViewConfigurationType type) {
    CHECK(m_instance != XR_NULL_HANDLE)
    CHECK(m_systemId != XR_NULL_SYSTEM_ID)

    uint32_t count;
    CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId, type, 0, &count,
                                                 nullptr))
    CHECK(count > 0)

    LOG_INFO("Available Environment Blend Mode count: (%u)", count);

    std::vector<XrEnvironmentBlendMode> blendModes(count);
    CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId, type, count, &count,
                                                 blendModes.data()))
    bool blendModeFound = false;
    for (auto mode: blendModes) {
        const bool blendModeMatch = (mode == options_->Parsed.EnvironmentBlendMode);
        LOG_INFO("Environment Blend Mode (%s): %s", to_string(mode), blendModeMatch ? "(Selected)" : "");
        blendModeFound |= blendModeMatch;
    }
    CHECK(blendModeFound)
}

XrEnvironmentBlendMode OpenXrProgram::GetPreferredBlendMode() const {
    uint32_t count;
    CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId,
                                                 options_->Parsed.ViewConfigType, 0,
                                                 &count,
                                                 nullptr))
    CHECK(count > 0)

    std::vector<XrEnvironmentBlendMode> blendModes(count);
    CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId,
                                                 options_->Parsed.ViewConfigType,
                                                 count, &count, blendModes.data()))
    for (const auto &blendMode: blendModes) {
        if (m_acceptableBlendModes.count(blendMode)) return blendMode;
    }
    THROW("No acceptable blend mode returned from the xrEnumerateEnvironmentBlendModes")
}

void OpenXrProgram::InitializeSystem() {
    CHECK(m_instance != XR_NULL_HANDLE)
    CHECK(m_systemId == XR_NULL_SYSTEM_ID)

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = options_->Parsed.FormFactor;
    CHECK_XRCMD(xrGetSystem(m_instance, &systemInfo, &m_systemId))

    LOG_INFO("Using system %llu for form factor %s", (unsigned long long) m_systemId, to_string(options_->Parsed.FormFactor));

    CHECK(m_instance != XR_NULL_HANDLE)
    CHECK(m_systemId != XR_NULL_SYSTEM_ID)
}

void OpenXrProgram::InitializeDevice() {
    LogViewConfigurations();

    graphicsContext_->InitializeDevice(m_instance, m_systemId);
}

void OpenXrProgram::LogReferenceSpaces() {
    CHECK(m_session != XR_NULL_HANDLE)

    uint32_t spaceCount;
    CHECK_XRCMD(xrEnumerateReferenceSpaces(m_session, 0, &spaceCount, nullptr))
    std::vector<XrReferenceSpaceType> spaces(spaceCount);
    CHECK_XRCMD(xrEnumerateReferenceSpaces(m_session, spaceCount, &spaceCount, spaces.data()))

    LOG_INFO("Available reference spaces: %d", spaceCount);
    for (auto space: spaces) {
        LOG_INFO("  Name: %s", to_string(space));
    }
}

void
OpenXrProgram::CreateAction(XrActionSet &actionSet, XrActionType type, const char *actionName, const char *localizedName, int countSubactionPaths,
                            const XrPath *subactionPaths, XrAction *action) {
    LOG_INFO("CreateAction %s, %d", actionName, countSubactionPaths);

    XrActionCreateInfo aci = {};
    aci.type = XR_TYPE_ACTION_CREATE_INFO;
    aci.next = nullptr;
    aci.actionType = type;

    aci.countSubactionPaths = countSubactionPaths;
    aci.subactionPaths = subactionPaths;

    strcpy(aci.actionName, actionName);
    strcpy(aci.localizedActionName, localizedName ? localizedName : actionName);

    CHECK_XRCMD(xrCreateAction(actionSet, &aci, action))
}

void OpenXrProgram::InitializeActions() {
    // Create an action set
    {
        XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy(actionSetInfo.actionSetName, "gameplay");
        strcpy(actionSetInfo.localizedActionSetName, "Gameplay");
        actionSetInfo.priority = 0;
        CHECK_XRCMD(xrCreateActionSet(m_instance, &actionSetInfo, &m_input.actionSet))
    }

    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right", &handSubactionPath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left", &handSubactionPath[Side::LEFT]))

    // Bind actions
    {
        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "quit_session", "Quit Session", 0, nullptr, &m_input.quitAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_POSE_INPUT, "controller_pose", "Controller Pose", Side::COUNT, handSubactionPath,
                     &m_input.controllerPoseAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick_pose", "Thumbstick Pose", Side::COUNT, handSubactionPath,
                     &m_input.thumbstickPoseAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_pressed", "Thumbstick Pressed", Side::COUNT, handSubactionPath,
                     &m_input.thumbstickPressedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_touched", "Thumbstick Touched", Side::COUNT, handSubactionPath,
                     &m_input.thumbstickTouchedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a_pressed", "Button A Pressed", 1, &handSubactionPath[Side::RIGHT],
                     &m_input.buttonAPressedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a_touched", "Button A Touched", 1, &handSubactionPath[Side::RIGHT],
                     &m_input.buttonATouchedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b_pressed", "Button B Pressed", 1, &handSubactionPath[Side::RIGHT],
                     &m_input.buttonBPressedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b_touched", "Button B Touched", 1, &handSubactionPath[Side::RIGHT],
                     &m_input.buttonBTouchedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x_pressed", "Button X Pressed", 1, &handSubactionPath[Side::LEFT],
                     &m_input.buttonXPressedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x_touched", "Button X Touched", 1, &handSubactionPath[Side::LEFT],
                     &m_input.buttonXTouchedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y_pressed", "Button Y Pressed", 1, &handSubactionPath[Side::LEFT],
                     &m_input.buttonYPressedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y_touched", "Button Y Touched", 1, &handSubactionPath[Side::LEFT],
                     &m_input.buttonYTouchedAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze_value", "Squeeze Value", 2, handSubactionPath,
                     &m_input.squeezeValueAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_FLOAT_INPUT, "trigger_value", "Trigger Value", 2, handSubactionPath,
                     &m_input.triggerValueAction);

        CreateAction(m_input.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "squeeze_touched", "Squeeze Touched", 2, handSubactionPath,
                     &m_input.triggerTouchedAction);
    }

    std::array<XrPath, Side::COUNT> menuClickPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/menu/click", &menuClickPath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/menu/click", &menuClickPath[Side::LEFT]))

    std::array<XrPath, Side::COUNT> controllerPosePath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/aim/pose", &controllerPosePath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/aim/pose", &controllerPosePath[Side::LEFT]))

    std::array<XrPath, Side::COUNT> thumbstickPosePath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/thumbstick", &thumbstickPosePath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/thumbstick", &thumbstickPosePath[Side::LEFT]))

    std::array<XrPath, Side::COUNT> thumbstickPressedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/thumbstick/click", &thumbstickPressedPath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/thumbstick/click", &thumbstickPressedPath[Side::LEFT]))

    std::array<XrPath, Side::COUNT> thumbstickTouchedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/thumbstick/touch", &thumbstickTouchedPath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/thumbstick/touch", &thumbstickTouchedPath[Side::LEFT]))

    XrPath buttonAPressedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/a/click", &buttonAPressedPath))
    XrPath buttonATouchedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/a/touch", &buttonATouchedPath))

    XrPath buttonBPressedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/b/click", &buttonBPressedPath))
    XrPath buttonBTouchedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/b/touch", &buttonBTouchedPath))

    XrPath buttonXPressedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/x/click", &buttonXPressedPath))
    XrPath buttonXTouchedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/x/touch", &buttonXTouchedPath))

    XrPath buttonYPressedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/y/click", &buttonYPressedPath))
    XrPath buttonYTouchedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/y/touch", &buttonYTouchedPath))

    std::array<XrPath, Side::COUNT> squeezeValuePath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/squeeze/value", &squeezeValuePath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/squeeze/value", &squeezeValuePath[Side::LEFT]))

    std::array<XrPath, Side::COUNT> triggerValuePath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/trigger/value", &triggerValuePath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/trigger/value", &triggerValuePath[Side::LEFT]))

    std::array<XrPath, Side::COUNT> triggerTouchedPath{};
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right/input/trigger/touch", &triggerTouchedPath[Side::RIGHT]))
    CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left/input/trigger/touch", &triggerTouchedPath[Side::LEFT]))

    {
        XrPath khrSimpleInteractionProfilePath;
        CHECK_XRCMD(xrStringToPath(m_instance, "/interaction_profiles/khr/simple_controller", &khrSimpleInteractionProfilePath))
        std::vector<XrActionSuggestedBinding> bindings{
                {
                        {m_input.quitAction, menuClickPath[Side::LEFT]},
                        {m_input.quitAction, menuClickPath[Side::RIGHT]}
                }
        };

        XrInteractionProfileSuggestedBinding suggestedBindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestedBindings.interactionProfile = khrSimpleInteractionProfilePath;
        suggestedBindings.suggestedBindings = bindings.data();
        suggestedBindings.countSuggestedBindings = (uint32_t) bindings.size();
        CHECK_XRCMD(xrSuggestInteractionProfileBindings(m_instance, &suggestedBindings))
    }

    {
        XrPath oculusTouchInteractionProfilePath;
        CHECK_XRCMD(xrStringToPath(m_instance, "/interaction_profiles/oculus/touch_controller", &oculusTouchInteractionProfilePath))
        std::vector<XrActionSuggestedBinding> bindings{
                {
                        {m_input.quitAction, menuClickPath[Side::LEFT]},
                        {m_input.controllerPoseAction, controllerPosePath[Side::LEFT]},
                        {m_input.controllerPoseAction, controllerPosePath[Side::RIGHT]},
                        {m_input.thumbstickPoseAction, thumbstickPosePath[Side::LEFT]},
                        {m_input.thumbstickPoseAction, thumbstickPosePath[Side::RIGHT]},
                        {m_input.thumbstickPressedAction, thumbstickPressedPath[Side::LEFT]},
                        {m_input.thumbstickPressedAction, thumbstickPressedPath[Side::RIGHT]},
                        {m_input.thumbstickTouchedAction, thumbstickTouchedPath[Side::LEFT]},
                        {m_input.thumbstickTouchedAction, thumbstickTouchedPath[Side::RIGHT]},
                        {m_input.buttonAPressedAction, buttonAPressedPath},
                        {m_input.buttonATouchedAction, buttonATouchedPath},
                        {m_input.buttonBPressedAction, buttonBPressedPath},
                        {m_input.buttonBTouchedAction, buttonBTouchedPath},
                        {m_input.buttonXPressedAction, buttonXPressedPath},
                        {m_input.buttonXTouchedAction, buttonXTouchedPath},
                        {m_input.buttonYPressedAction, buttonYPressedPath},
                        {m_input.buttonYTouchedAction, buttonYTouchedPath},
                        {m_input.squeezeValueAction, squeezeValuePath[Side::LEFT]},
                        {m_input.squeezeValueAction, squeezeValuePath[Side::RIGHT]},
                        {m_input.triggerValueAction, triggerValuePath[Side::LEFT]},
                        {m_input.triggerValueAction, triggerValuePath[Side::RIGHT]},
                        {m_input.triggerTouchedAction, triggerTouchedPath[Side::LEFT]},
                        {m_input.triggerTouchedAction, triggerTouchedPath[Side::RIGHT]},
                }
        };
        XrInteractionProfileSuggestedBinding suggestedBindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestedBindings.interactionProfile = oculusTouchInteractionProfilePath;
        suggestedBindings.suggestedBindings = bindings.data();
        suggestedBindings.countSuggestedBindings = (uint32_t) bindings.size();
        CHECK_XRCMD(xrSuggestInteractionProfileBindings(m_instance, &suggestedBindings))
    }

    XrActionSpaceCreateInfo actionSpaceInfo = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
    actionSpaceInfo.action = m_input.controllerPoseAction;
    actionSpaceInfo.poseInActionSpace.orientation.w = 1.0f;
    actionSpaceInfo.subactionPath = handSubactionPath[Side::LEFT];
    CHECK_XRCMD(xrCreateActionSpace(m_session, &actionSpaceInfo, &controllerSpace[Side::LEFT]))
    actionSpaceInfo.subactionPath = handSubactionPath[Side::RIGHT];
    CHECK_XRCMD(xrCreateActionSpace(m_session, &actionSpaceInfo, &controllerSpace[Side::RIGHT]))

    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &m_input.actionSet;
    CHECK_XRCMD(xrAttachSessionActionSets(m_session, &attachInfo))
}

void OpenXrProgram::CreateVisualizedSpaces() {
    CHECK(m_session != XR_NULL_HANDLE)

    std::string visualizedSpaces[] = {
            "ViewFront", "Local", "Stage", "StageLeft", "StageRight",
            "StageLeftRotated", "StageRightRotated"
    };

    for (const auto &visualizedSpace: visualizedSpaces) {
        XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = GetXrReferenceSpaceCreateInfo(visualizedSpace);
        XrSpace space;
        XrResult res = xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &space);
        if (XR_SUCCEEDED(res)) {
            m_visualizedSpaces.push_back(space);
        } else {
            LOG_INFO("Failed to create reference space %s with error %d", visualizedSpace.c_str(), res);
        }
    }
}

void OpenXrProgram::InitializeSession() {
    CHECK(m_instance != XR_NULL_HANDLE)
    CHECK(m_session == XR_NULL_HANDLE)

    {
        LOG_INFO("Creating session...");
        XrSessionCreateInfo createInfo{XR_TYPE_SESSION_CREATE_INFO};
        createInfo.next = graphicsContext_->GetGraphicsBinding();
        createInfo.systemId = m_systemId;

        CHECK_XRCMD(xrCreateSession(m_instance, &createInfo, &m_session))
    }

    LogReferenceSpaces();

    InitializeActions();

    CreateVisualizedSpaces();

    {
        XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = GetXrReferenceSpaceCreateInfo(options_->AppSpace);
        CHECK_XRCMD(xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &m_appSpace))
    }
}

void OpenXrProgram::InitializeRendering() {
    CHECK(m_instance != XR_NULL_HANDLE)
    CHECK(m_session != XR_NULL_HANDLE)

    graphicsContext_->InitializeRendering(m_instance, m_systemId, m_session);
}

void OpenXrProgram::InitializeStreaming() {
    gstreamerPlayer.play();
}

void OpenXrProgram::InitializeControllerStream() {
    udpSocket = createSocket();
}

void OpenXrProgram::SendControllerDatagram() {
    sendUDPPacket(udpSocket, userState);
    if (!servoComm.servosEnabled()) {
        servoComm.enableServos(true, threadPool);
    }
    if (servoComm.isReady()) {
        servoComm.setPose(userState.hmdPose.orientation, threadPool);
    }
}

const XrEventDataBaseHeader *OpenXrProgram::TryReadNextEvent() {
    auto *baseHeader = reinterpret_cast<XrEventDataBaseHeader *>(&m_eventDataBuffer);
    *baseHeader = {XR_TYPE_EVENT_DATA_BUFFER};
    const XrResult xr = xrPollEvent(m_instance, &m_eventDataBuffer);
    if (xr == XR_SUCCESS) {
        if (baseHeader->type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
            const auto *const eventsLost = reinterpret_cast<const XrEventDataEventsLost *>(baseHeader);
            LOG_INFO("%d events lost", eventsLost->lostEventCount);
        }
        return baseHeader;
    }
    if (xr == XR_EVENT_UNAVAILABLE) {
        return nullptr;
    }
    THROW_XR(xr, "xrPollEvent")
}

void OpenXrProgram::PollEvents(bool *exitRenderLoop, bool *requestRestart) {
    *exitRenderLoop = *requestRestart = false;

    while (const XrEventDataBaseHeader *event = TryReadNextEvent()) {
        switch (event->type) {
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                const auto &instanceLossPending = *reinterpret_cast<const XrEventDataInstanceLossPending *>(event);
                LOG_INFO("XrEventDataInstanceLossPending by %lld",
                         (unsigned long long) instanceLossPending.lossTime);
                *exitRenderLoop = true;
                *requestRestart = true;
                break;
            }
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto sessionStateChangedEvent = *reinterpret_cast<const XrEventDataSessionStateChanged *>(event);
                HandleSessionStateChangedEvent(sessionStateChangedEvent, exitRenderLoop,
                                               requestRestart);
                break;
            }
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                LogActionSourceName(m_input.quitAction, "Quit");
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            default: {
                LOG_INFO("Ignoring event type %d", event->type);
                break;
            }
        }
    }
}

void OpenXrProgram::HandleSessionStateChangedEvent(const XrEventDataSessionStateChanged &stateChangedEvent,
                                                   bool *exitRenderLoop, bool *requestRestart) {
    const XrSessionState oldState = m_sessionState;
    m_sessionState = stateChangedEvent.state;

    LOG_INFO("XrEventDataSessionStateChanged: state %s->%s session=%lld time=%lld",
             to_string(oldState), to_string(m_sessionState),
             (unsigned long long) stateChangedEvent.session,
             (unsigned long long) stateChangedEvent.time);

    if ((stateChangedEvent.session != XR_NULL_HANDLE) &&
        (stateChangedEvent.session != m_session)) {
        LOG_ERROR("XrEVentDataSessionStateChanged for unknown session");
        return;
    }

    switch (m_sessionState) {
        case XR_SESSION_STATE_READY: {
            CHECK(m_session != XR_NULL_HANDLE)
            XrSessionBeginInfo sessionBeginInfo{XR_TYPE_SESSION_BEGIN_INFO};
            sessionBeginInfo.primaryViewConfigurationType = options_->Parsed.ViewConfigType;
            CHECK_XRCMD(xrBeginSession(m_session, &sessionBeginInfo))
            m_sessionRunning = true;
            break;
        }
        case XR_SESSION_STATE_STOPPING: {
            CHECK(m_session != XR_NULL_HANDLE)
            m_sessionRunning = false;
            CHECK_XRCMD(xrEndSession(m_session))
            break;
        }
        case XR_SESSION_STATE_EXITING: {
            *exitRenderLoop = true;
            *requestRestart = false;
            break;
        }
        case XR_SESSION_STATE_LOSS_PENDING: {
            *exitRenderLoop = true;
            *requestRestart = true;
            break;
        }
        default:
            break;
    }
}

void OpenXrProgram::LogActionSourceName(XrAction action, const std::string &actionName) {
    XrBoundSourcesForActionEnumerateInfo getInfo = {
            XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO};
    getInfo.action = action;
    uint32_t pathCount = 0;
    CHECK_XRCMD(xrEnumerateBoundSourcesForAction(m_session, &getInfo, 0, &pathCount, nullptr))
    std::vector<XrPath> paths(pathCount);
    CHECK_XRCMD(xrEnumerateBoundSourcesForAction(m_session, &getInfo, uint32_t(paths.size()),
                                                 &pathCount, paths.data()))

    std::string sourceName;
    for (uint32_t i = 0; i < pathCount; ++i) {
        constexpr XrInputSourceLocalizedNameFlags all =
                XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT |
                XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT |
                XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT;
        XrInputSourceLocalizedNameGetInfo nameInfo = {
                XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO};
        nameInfo.sourcePath = paths[i];
        nameInfo.whichComponents = all;

        uint32_t size = 0;
        CHECK_XRCMD(xrGetInputSourceLocalizedName(m_session, &nameInfo, 0, &size, nullptr))
        std::vector<char> grabSource(size);
        CHECK_XRCMD(
                xrGetInputSourceLocalizedName(m_session, &nameInfo, uint32_t(grabSource.size()),
                                              &size, grabSource.data()))
        if (!sourceName.empty()) {
            sourceName += " and ";
        }
        sourceName += "'";
        sourceName += std::string(grabSource.data(), size - 1);
        sourceName += "'";
    }

    LOG_INFO("%s action is bound to %s", actionName.c_str(),
             ((!sourceName.empty()) ? sourceName.c_str() : "nothing"));
}

void OpenXrProgram::PollActions() {
    const XrActiveActionSet activeActionSet{m_input.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    CHECK_XRCMD(xrSyncActions(m_session, &syncInfo))

    XrActionStateGetInfo getQuitInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.quitAction,
                                     XR_NULL_PATH};
    XrActionStateBoolean quitValue{XR_TYPE_ACTION_STATE_BOOLEAN};
    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getQuitInfo, &quitValue))
    if ((quitValue.isActive == XR_TRUE) && (quitValue.changedSinceLastSync == XR_TRUE) &&
        (quitValue.currentState == XR_TRUE)) {
        CHECK_XRCMD(xrRequestExitSession(m_session))
    }

    // Thumbstick pose
    XrActionStateGetInfo getThumbstickPoseRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.thumbstickPoseAction,
                                                    handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getThumbstickPoseLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.thumbstickPoseAction,
                                                   handSubactionPath[Side::LEFT]};
    XrActionStateVector2f thumbstickPose{XR_TYPE_ACTION_STATE_VECTOR2F};

    CHECK_XRCMD(xrGetActionStateVector2f(m_session, &getThumbstickPoseRightInfo, &thumbstickPose))
    userState.thumbstickPose[Side::RIGHT] = thumbstickPose.currentState;

    CHECK_XRCMD(xrGetActionStateVector2f(m_session, &getThumbstickPoseLeftInfo, &thumbstickPose))
    userState.thumbstickPose[Side::LEFT] = thumbstickPose.currentState;

    // Thumbstick pressed
    XrActionStateGetInfo getThumbstickPressedRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.thumbstickPressedAction,
                                                       handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getThumbstickPressedLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.thumbstickPressedAction,
                                                      handSubactionPath[Side::LEFT]};
    XrActionStateBoolean thumbstickPressed{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getThumbstickPressedRightInfo, &thumbstickPressed))
    userState.thumbstickPressed[Side::RIGHT] = thumbstickPressed.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getThumbstickPressedLeftInfo, &thumbstickPressed))
    userState.thumbstickPressed[Side::LEFT] = thumbstickPressed.currentState;

    // Thumbstick touched
    XrActionStateGetInfo getThumbstickTouchedRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.thumbstickTouchedAction,
                                                       handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getThumbstickTouchedLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.thumbstickTouchedAction,
                                                      handSubactionPath[Side::LEFT]};
    XrActionStateBoolean thumbstickTouched{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getThumbstickTouchedRightInfo, &thumbstickTouched))
    userState.thumbstickTouched[Side::RIGHT] = thumbstickTouched.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getThumbstickTouchedLeftInfo, &thumbstickTouched))
    userState.thumbstickTouched[Side::LEFT] = thumbstickTouched.currentState;

    // Button A
    XrActionStateGetInfo getButtonAPressedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.buttonAPressedAction, XR_NULL_PATH};
    XrActionStateGetInfo getButtonATouchedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.buttonATouchedAction, XR_NULL_PATH};
    XrActionStateBoolean buttonA{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getButtonAPressedInfo, &buttonA))
    userState.aPressed = buttonA.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getButtonATouchedInfo, &buttonA))
    userState.aTouched = buttonA.currentState;

    // Button B
    XrActionStateGetInfo getButtonBPressedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.buttonBPressedAction, XR_NULL_PATH};
    XrActionStateGetInfo getButtonBTouchedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.buttonBTouchedAction, XR_NULL_PATH};
    XrActionStateBoolean buttonB{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getButtonBPressedInfo, &buttonB))
    userState.bPressed = buttonB.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getButtonBTouchedInfo, &buttonB))
    userState.bTouched = buttonB.currentState;

    // Button X
    XrActionStateGetInfo getButtonXPressedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.buttonXPressedAction, XR_NULL_PATH};
    XrActionStateGetInfo getButtonXTouchedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.buttonXTouchedAction, XR_NULL_PATH};
    XrActionStateBoolean buttonX{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getButtonXPressedInfo, &buttonX))
    userState.xPressed = buttonX.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getButtonXTouchedInfo, &buttonX))
    userState.xTouched = buttonX.currentState;

    // Button Y
    XrActionStateGetInfo getButtonYPressedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.buttonYPressedAction, XR_NULL_PATH};
    XrActionStateGetInfo getButtonYTouchedInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.buttonYTouchedAction, XR_NULL_PATH};
    XrActionStateBoolean buttonY{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getButtonYPressedInfo, &buttonY))
    userState.yPressed = buttonY.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getButtonYTouchedInfo, &buttonY))
    userState.yTouched = buttonY.currentState;

    // Squeeze
    XrActionStateGetInfo getSqueezeValueRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.squeezeValueAction,
                                                  handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getSqueezeValueLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.squeezeValueAction,
                                                 handSubactionPath[Side::LEFT]};
    XrActionStateFloat squeezeValue{XR_TYPE_ACTION_STATE_FLOAT};

    CHECK_XRCMD(xrGetActionStateFloat(m_session, &getSqueezeValueRightInfo, &squeezeValue))
    userState.squeezeValue[Side::RIGHT] = squeezeValue.currentState;

    CHECK_XRCMD(xrGetActionStateFloat(m_session, &getSqueezeValueLeftInfo, &squeezeValue))
    userState.squeezeValue[Side::LEFT] = squeezeValue.currentState;

    // Trigger value
    XrActionStateGetInfo getTriggerValueRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.triggerValueAction,
                                                  handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getTriggerValueLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.triggerValueAction,
                                                 handSubactionPath[Side::LEFT]};
    XrActionStateFloat triggerValue{XR_TYPE_ACTION_STATE_FLOAT};

    CHECK_XRCMD(xrGetActionStateFloat(m_session, &getTriggerValueRightInfo, &triggerValue))
    userState.triggerValue[Side::RIGHT] = triggerValue.currentState;

    CHECK_XRCMD(xrGetActionStateFloat(m_session, &getTriggerValueLeftInfo, &triggerValue))
    userState.triggerValue[Side::LEFT] = triggerValue.currentState;

    // Trigger touched
    XrActionStateGetInfo getTriggerTouchedRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.triggerTouchedAction,
                                                    handSubactionPath[Side::RIGHT]};
    XrActionStateGetInfo getTriggerTouchedLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.triggerTouchedAction,
                                                   handSubactionPath[Side::LEFT]};
    XrActionStateBoolean triggerTouched{XR_TYPE_ACTION_STATE_BOOLEAN};

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getTriggerTouchedRightInfo, &triggerTouched))
    userState.triggerTouched[Side::RIGHT] = triggerTouched.currentState;

    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getTriggerTouchedLeftInfo, &triggerTouched))
    userState.triggerTouched[Side::LEFT] = triggerTouched.currentState;
}

void OpenXrProgram::PollPoses(XrTime predictedDisplayTime) {
    const XrActiveActionSet activeActionSet{m_input.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    CHECK_XRCMD(xrSyncActions(m_session, &syncInfo))

    // Controller poses
    for (int i = 0; i < Side::COUNT; i++) {
        XrSpaceVelocity vel = {XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
        loc.next = &vel;

        CHECK_XRCMD(xrLocateSpace(controllerSpace[i], m_appSpace, predictedDisplayTime, &loc))
        if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
            userState.controllerPose[i] = loc.pose;
        }
    }
}

void OpenXrProgram::RenderFrame() {
    CHECK(m_session != XR_NULL_HANDLE)

    // Create and cache view buffer for xrLocateViews later.
    m_views.resize(2, {XR_TYPE_VIEW});

    XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    CHECK_XRCMD(xrWaitFrame(m_session, &frameWaitInfo, &frameState))

    XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    CHECK_XRCMD(xrBeginFrame(m_session, &frameBeginInfo))

    PollPoses(frameState.predictedDisplayTime);

    std::vector<XrCompositionLayerBaseHeader *> layers;
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::vector<XrCompositionLayerProjectionView> projectionLayerViews;
    if (frameState.shouldRender == XR_TRUE) {
        if (RenderLayer(frameState.predictedDisplayTime, projectionLayerViews, layer)) {
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer));
        }
    }

    XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
    frameEndInfo.displayTime = frameState.predictedDisplayTime;
    frameEndInfo.environmentBlendMode = options_->Parsed.EnvironmentBlendMode;
    frameEndInfo.layerCount = (uint32_t) layers.size();
    frameEndInfo.layers = layers.data();
    CHECK_XRCMD(xrEndFrame(m_session, &frameEndInfo))
}

bool OpenXrProgram::RenderLayer(XrTime predictedDisplayTime, std::vector<XrCompositionLayerProjectionView> &projectionLayerViews,
                                XrCompositionLayerProjection &layer) {
    XrResult res;

    XrViewState viewState{XR_TYPE_VIEW_STATE};
    auto viewCapacityInput = (uint32_t) m_views.size();
    uint32_t viewCountOutput;

    XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    viewLocateInfo.viewConfigurationType = options_->Parsed.ViewConfigType;
    viewLocateInfo.displayTime = predictedDisplayTime;
    viewLocateInfo.space = m_appSpace;

    res = xrLocateViews(m_session, &viewLocateInfo, &viewState, viewCapacityInput, &viewCountOutput, m_views.data());
    CHECK_XRRESULT(res, "xrLocateViews")
    if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0 ||
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
        return false;
    }

    CHECK(viewCountOutput == viewCapacityInput)

    projectionLayerViews.resize(viewCountOutput);

    //Quad quad{};

    XrSpaceLocation spaceLocation{XR_TYPE_SPACE_LOCATION};

    // Locate "ViewFront" space
    res = xrLocateSpace(m_visualizedSpaces[0], m_appSpace, predictedDisplayTime,
                        &spaceLocation);
    CHECK_XRRESULT(res, "xrLocateSpace")
    if (XR_UNQUALIFIED_SUCCESS(res)) {
        if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
            (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
            //quad = Quad{spaceLocation.pose, {4.45f, 2.5f, 0.0f}};
            userState.hmdPose = spaceLocation.pose;
        }
    } else {
        LOG_INFO("Unable to locate a visualized reference space in app space: %d", res);
    }

    // Render view to the appropriate part of the swapchain image.
    for (uint32_t i = 0; i < viewCountOutput; i++) {

        projectionLayerViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projectionLayerViews[i].pose = m_views[i].pose;
        projectionLayerViews[i].fov = m_views[i].fov;

        graphicsContext_->RenderView(projectionLayerViews[i], (Geometry::DisplayType) i,
                                     i == 0 ? gstreamerPlayer.getFrameRight().dataHandle
                                            : gstreamerPlayer.getFrameLeft().dataHandle);
    }

    layer.space = m_appSpace;
    layer.layerFlags =
            options_->Parsed.EnvironmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND
            ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
              XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT
            : 0;
    layer.viewCount = (uint32_t) projectionLayerViews.size();
    layer.views = projectionLayerViews.data();

    return true;
}

XrReferenceSpaceCreateInfo OpenXrProgram::GetXrReferenceSpaceCreateInfo(const std::string &referenceSpaceTypeStr) {
    XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    referenceSpaceCreateInfo.poseInReferenceSpace = Identity();
    if (EqualsIgnoreCase(referenceSpaceTypeStr, "View")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "ViewFront")) {
        // Render head-locked 2m in front of device.
        referenceSpaceCreateInfo.poseInReferenceSpace = Translation({0.f, 0.f, -2.f}),
                referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Local")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Stage")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeft")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = RotateCCWAboutYAxis(0.f, {-2.f, 0.f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRight")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = RotateCCWAboutYAxis(0.f, {2.f, 0.f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeftRotated")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = RotateCCWAboutYAxis(3.14f / 3.f, {-2.f, 0.5f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRightRotated")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = RotateCCWAboutYAxis(-3.14f / 3.f, {2.f, 0.5f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else {
        throw std::invalid_argument(
                Fmt("Unknown reference space type '%s'", referenceSpaceTypeStr.c_str()));
    }
    return referenceSpaceCreateInfo;
}

