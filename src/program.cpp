
#include "pch.h"
#include "log.h"
#include "check.h"
#include "graphics_plugin.h"
#include "platform_plugin.h"
#include "BS_thread_pool.hpp"
#include "gstreamer_player.h"
#include "program.h"


#include <utility>

namespace Side {
    const int LEFT = 0;
    const int RIGHT = 1;
    const int COUNT = 2;
}  // namespace Side

namespace Math::Pose {
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
}

inline XrReferenceSpaceCreateInfo
GetXrReferenceSpaceCreateInfo(const std::string &referenceSpaceTypeStr) {
    XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::Identity();
    if (EqualsIgnoreCase(referenceSpaceTypeStr, "View")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "ViewFront")) {
        // Render head-locked 2m in front of device.
        referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::Translation({0.f, 0.f, -2.f}),
                referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Local")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Stage")) {
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeft")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(0.f, {-2.f, 0.f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRight")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(0.f, {2.f, 0.f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeftRotated")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(3.14f / 3.f, {-2.f, 0.5f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRightRotated")) {
        referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(-3.14f / 3.f, {2.f, 0.5f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else {
        throw std::invalid_argument(Fmt("Unknown reference space type '%s'", referenceSpaceTypeStr.c_str()));
    }
    return referenceSpaceCreateInfo;
}


struct OpenXrProgram : IOpenXrProgram {
    OpenXrProgram(std::shared_ptr<IPlatformPlugin> platformPlugin, std::shared_ptr<IGraphicsPlugin> graphicsPlugin)
            : m_platformPlugin(std::move(platformPlugin)), m_graphicsPlugin(std::move(graphicsPlugin)),
              m_acceptableBlendModes{XR_ENVIRONMENT_BLEND_MODE_OPAQUE, XR_ENVIRONMENT_BLEND_MODE_ADDITIVE,
                                     XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND} {}

    ~OpenXrProgram() override = default;

    static void LogLayersAndExtensions() {
        const auto logExtensions = [](const char *layerName, int indent = 0) {
            uint32_t instanceExtensionCount;
            CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(layerName, 0, &instanceExtensionCount,
                                                               nullptr))

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

    void LogInstanceInfo() {
        CHECK(m_instance != XR_NULL_HANDLE)

        XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
        CHECK_XRCMD(xrGetInstanceProperties(m_instance, &instanceProperties))

        LOG_INFO("Instance RuntimeName=%s RuntimeVersion=%s", instanceProperties.runtimeName,
                 GetXrVersionString(instanceProperties.runtimeVersion).c_str());
    }

    void CreateInstanceInternal() {
        CHECK(m_instance == XR_NULL_HANDLE)

        // Create union of extensions required by platform and graphics plugins.
        std::vector<const char *> extensions;

        // Transform platform and graphics extension std::strings to C strings.
        const std::vector<std::string> platformExtensions = m_platformPlugin->GetInstanceExtensions();
        std::transform(platformExtensions.begin(), platformExtensions.end(),
                       std::back_inserter(extensions),
                       [](const std::string &ext) { return ext.c_str(); });
        const std::vector<std::string> graphicsExtensions = m_graphicsPlugin->GetInstanceExtensions();
        std::transform(graphicsExtensions.begin(), graphicsExtensions.end(),
                       std::back_inserter(extensions),
                       [](const std::string &ext) { return ext.c_str(); });

        XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        createInfo.next = m_platformPlugin->GetInstanceCreateExtension();
        createInfo.enabledExtensionCount = (uint32_t) extensions.size();
        createInfo.enabledExtensionNames = extensions.data();

        strcpy(createInfo.applicationInfo.applicationName, "BUT_Telepresence");
        createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

        CHECK_XRCMD(xrCreateInstance(&createInfo, &m_instance))
    }

    void CreateInstance() override {
        LogLayersAndExtensions();

        CreateInstanceInternal();

        LogInstanceInfo();
    }

    void LogViewConfigurations() {
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
                     viewConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ? "(Selected)" : "");

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

    void LogEnvironmentBlendMode(XrViewConfigurationType type) {
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
            const bool blendModeMatch = (mode == m_preferredBlendMode);
            LOG_INFO("Environment Blend Mode (%s): %s", to_string(mode),
                     blendModeMatch ? "(Selected)" : "");
            blendModeFound |= blendModeMatch;
        }
        CHECK(blendModeFound)
    }

    XrEnvironmentBlendMode GetPreferredBlendMode() const override {
        return m_preferredBlendMode;
    }

    void UpdatePreferredBlendMode() {
        uint32_t count;
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId,
                                                     XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                     &count,
                                                     nullptr))
        CHECK(count > 0)

        std::vector<XrEnvironmentBlendMode> blendModes(count);
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId,
                                                     XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                     count, &count, blendModes.data()))
        for (const auto &blendMode: blendModes) {
            if (m_acceptableBlendModes.count(blendMode)) {
                LOG_INFO("Preferred Blend Mode: %s", to_string(blendMode));
                m_preferredBlendMode = blendMode;
                return;
            }
        }
        THROW("No acceptable blend mode returned from the xrEnumerateEnvironmentBlendModes")
    }

    void InitializeSystem() override {
        CHECK(m_instance != XR_NULL_HANDLE)
        CHECK(m_systemId == XR_NULL_SYSTEM_ID)

        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        CHECK_XRCMD(xrGetSystem(m_instance, &systemInfo, &m_systemId))

        LOG_INFO("Using system %llu for form factor %s", (unsigned long long) m_systemId, to_string(XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY));

        CHECK(m_systemId != XR_NULL_SYSTEM_ID)

        UpdatePreferredBlendMode();
    }

    void InitializeDevice() override {
        LogViewConfigurations();

        m_graphicsPlugin->InitializeDevice(m_instance, m_systemId);
    }

    void LogReferenceSpaces() {
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

    struct InputState {
        XrActionSet actionSet{XR_NULL_HANDLE};
        XrAction quitAction{XR_NULL_HANDLE};
        XrAction controllerPoseAction{XR_NULL_HANDLE};
        XrAction thumbstickAction{XR_NULL_HANDLE};
    };

    void InitializeActions() {
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
            XrActionCreateInfo quitActionInfo{XR_TYPE_ACTION_CREATE_INFO};
            quitActionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
            strcpy(quitActionInfo.actionName, "quit_session");
            strcpy(quitActionInfo.localizedActionName, "Quit Session");
            quitActionInfo.countSubactionPaths = 0;
            quitActionInfo.subactionPaths = nullptr;
            CHECK_XRCMD(xrCreateAction(m_input.actionSet, &quitActionInfo, &m_input.quitAction))

            XrActionCreateInfo controllerPoseActionInfo{XR_TYPE_ACTION_CREATE_INFO};
            controllerPoseActionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
            strcpy(controllerPoseActionInfo.actionName, "controller_pose");
            strcpy(controllerPoseActionInfo.localizedActionName, "Controller Pose");
            controllerPoseActionInfo.countSubactionPaths = Side::COUNT;
            controllerPoseActionInfo.subactionPaths = handSubactionPath;
            CHECK_XRCMD(xrCreateAction(m_input.actionSet, &controllerPoseActionInfo, &m_input.controllerPoseAction))

            XrActionCreateInfo thumbstickPoseActionInfo{XR_TYPE_ACTION_CREATE_INFO};
            thumbstickPoseActionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
            strcpy(thumbstickPoseActionInfo.actionName, "thumbstick");
            strcpy(thumbstickPoseActionInfo.localizedActionName, "Thumbstick");
            thumbstickPoseActionInfo.countSubactionPaths = Side::COUNT;
            thumbstickPoseActionInfo.subactionPaths = handSubactionPath;
            CHECK_XRCMD(xrCreateAction(m_input.actionSet, &thumbstickPoseActionInfo, &m_input.thumbstickAction))
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

        {
            XrPath khrSimpleInteractionProfilePath;
            CHECK_XRCMD(xrStringToPath(m_instance, "/interaction_profiles/khr/simple_controller",
                                       &khrSimpleInteractionProfilePath))
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
            CHECK_XRCMD(xrStringToPath(m_instance, "/interaction_profiles/oculus/touch_controller",
                                       &oculusTouchInteractionProfilePath))
            std::vector<XrActionSuggestedBinding> bindings{
                    {
                            {m_input.quitAction, menuClickPath[Side::LEFT]},
                            {m_input.controllerPoseAction, controllerPosePath[Side::LEFT]},
                            {m_input.controllerPoseAction, controllerPosePath[Side::RIGHT]},
                            {m_input.thumbstickAction, thumbstickPosePath[Side::LEFT]},
                            {m_input.thumbstickAction, thumbstickPosePath[Side::RIGHT]},
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

    void CreateVisualizedSpaces() {
        CHECK(m_session != XR_NULL_HANDLE)

        std::string visualizedSpaces[] = {
                "ViewFront", "Local", "Stage", "StageLeft", "StageRight",
                "StageLeftRotated", "StageRightRotated"
        };

        for (const auto &visualizedSpace: visualizedSpaces) {
            XrReferenceSpaceCreateInfo referenceSpaceCreateInfo
                    = GetXrReferenceSpaceCreateInfo(visualizedSpace);
            XrSpace space;
            XrResult res = xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &space);
            if (XR_SUCCEEDED(res)) {
                m_visualizedSpaces.push_back(space);
            } else {
                LOG_INFO("Failed to create reference space %s with error %d",
                         visualizedSpace.c_str(), res);
            }
        }
    }

    void InitializeSession() override {
        CHECK(m_instance != XR_NULL_HANDLE)
        CHECK(m_session == XR_NULL_HANDLE)

        {
            LOG_INFO("Creating session...");
            XrSessionCreateInfo createInfo{XR_TYPE_SESSION_CREATE_INFO};
            createInfo.next = m_graphicsPlugin->GetGraphicsBinding();
            createInfo.systemId = m_systemId;
            CHECK_XRCMD(xrCreateSession(m_instance, &createInfo, &m_session))
        }

        LogReferenceSpaces();
        InitializeActions();
        CreateVisualizedSpaces();

        {
            XrReferenceSpaceCreateInfo referenceSpaceCreateInfo
                    = GetXrReferenceSpaceCreateInfo("Local");
            CHECK_XRCMD(xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &m_appSpace))
        }
    }

    void InitializeStreaming() override {
        gstreamerPlayer.play();
    }

    void CreateSwapchains() override {
        CHECK(m_session != XR_NULL_HANDLE)
        CHECK(m_swapchains.empty())
        CHECK(m_configViews.empty())

        // Read graphics properties for preferred swapchain length and logging
        XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
        CHECK_XRCMD(xrGetSystemProperties(m_instance, m_systemId, &systemProperties))

        // Log system properties
        LOG_INFO("System Properties: Name=%s VendorId=%d", systemProperties.systemName,
                 systemProperties.vendorId);
        LOG_INFO("System Graphics Properties: MaxWidth=%d MaxHeight=%d MaxLayers=%d",
                 systemProperties.graphicsProperties.maxSwapchainImageWidth,
                 systemProperties.graphicsProperties.maxSwapchainImageHeight,
                 systemProperties.graphicsProperties.maxLayerCount);
        LOG_INFO("System Tracking Properties: OrientationTracking=%s PositionTracking=%s",
                 systemProperties.trackingProperties.orientationTracking == XR_TRUE ? "True"
                                                                                    : "False",
                 systemProperties.trackingProperties.positionTracking == XR_TRUE ? "True"
                                                                                 : "False");
        // Query and cache view configuration views
        uint32_t viewCount;
        CHECK_XRCMD(xrEnumerateViewConfigurationViews(
                m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0, &viewCount, nullptr))
        m_configViews.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        CHECK_XRCMD(xrEnumerateViewConfigurationViews(
                m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                viewCount, &viewCount, m_configViews.data()))


        // Create and cache view buffer for xrLocateViews later.
        m_views.resize(viewCount, {XR_TYPE_VIEW});

        if (viewCount > 0) {
            uint32_t swapchainFormatCount;
            CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session, 0, &swapchainFormatCount, nullptr))
            std::vector<int64_t> swapchainFormats(swapchainFormatCount);
            CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session, swapchainFormatCount,
                                                    &swapchainFormatCount,
                                                    swapchainFormats.data()))
            m_colorSwapchainFormat = m_graphicsPlugin->SelectColorSwapchainFormat(swapchainFormats);

            // Print swapchain formats and the selected one
            {
                std::string swapchainFormatsString;
                for (int64_t format: swapchainFormats) {
                    const bool selected = format == m_colorSwapchainFormat;
                    swapchainFormatsString += " ";
                    if (selected) {
                        swapchainFormatsString += "[";
                    }
                    swapchainFormatsString += std::to_string(format);
                    if (selected) {
                        swapchainFormatsString += "]";
                    }
                }
                LOG_INFO("Swapchain Formats: %s", swapchainFormatsString.c_str());
            }

            // Create a swapchain for each view.
            for (uint32_t i = 0; i < viewCount; i++) {
                const XrViewConfigurationView &vp = m_configViews[i];
                LOG_INFO(
                        "Creating swapchain for view %d with dimensions Width=%d Height=%d SampleCount=%d",
                        i, vp.recommendedImageRectHeight, vp.recommendedImageRectHeight,
                        vp.recommendedSwapchainSampleCount);

                // Create a swapchain
                XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
                swapchainCreateInfo.arraySize = 1;
                swapchainCreateInfo.format = m_colorSwapchainFormat;
                swapchainCreateInfo.width = vp.recommendedImageRectWidth;
                swapchainCreateInfo.height = vp.recommendedImageRectHeight;
                swapchainCreateInfo.mipCount = 1;
                swapchainCreateInfo.faceCount = 1;
                swapchainCreateInfo.sampleCount = m_graphicsPlugin->GetSupportedSwapchainSampleCount(
                        vp);
                swapchainCreateInfo.usageFlags =
                        XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
                Swapchain swapchain{};
                swapchain.width = static_cast<int32_t>(swapchainCreateInfo.width);
                swapchain.height = static_cast<int32_t>(swapchainCreateInfo.height);
                CHECK_XRCMD(xrCreateSwapchain(m_session, &swapchainCreateInfo, &swapchain.handle))

                m_swapchains.push_back(swapchain);
                uint32_t imageCount;
                CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr))
                std::vector<XrSwapchainImageBaseHeader *> swapchainImages =
                        m_graphicsPlugin->AllocateSwapchainImageStructs(imageCount,
                                                                        swapchainCreateInfo);
                CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount,
                                                       swapchainImages[0]))
                m_swapchainImages.insert(
                        std::make_pair(swapchain.handle, std::move(swapchainImages)));
            }
        }
    }

    const XrEventDataBaseHeader *TryReadNextEvent() {
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

    void PollEvents(bool *exitRenderLoop, bool *requestRestart) override {
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

    void HandleSessionStateChangedEvent(const XrEventDataSessionStateChanged &stateChangedEvent,
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
                sessionBeginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
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

    void LogActionSourceName(XrAction action, const std::string &actionName) const {
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

    bool IsSessionRunning() const override { return m_sessionRunning; }

    bool IsSessionFocused() const override { return m_sessionState == XR_SESSION_STATE_FOCUSED; }

    void PollActions() override {
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

        //TODO: Thumbstick positions here!

        // Thumbsticks
        XrActionStateGetInfo getThumbstickRightInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.thumbstickAction, handSubactionPath[1]};
        XrActionStateGetInfo getThumbstickLeftInfo{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.thumbstickAction, handSubactionPath[0]};
        XrActionStateVector2f thumbstickValue{XR_TYPE_ACTION_STATE_VECTOR2F};

        CHECK_XRCMD(xrGetActionStateVector2f(m_session, &getThumbstickRightInfo, &thumbstickValue))
        //LOG_INFO("Left thumbstick pose: %f %f", thumbstickValue.currentState.x, thumbstickValue.currentState.y);

        CHECK_XRCMD(xrGetActionStateVector2f(m_session, &getThumbstickLeftInfo, &thumbstickValue))
        //LOG_INFO("Right thumbstick pose: %f %f", thumbstickValue.currentState.x, thumbstickValue.currentState.y);
    }

    void PollPoses(XrTime predictedDisplayTime) {
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
                //TODO: Controller Pose here!
//                LOG_INFO("%s controller pose: pos: %f %f %f ori: %f %f %f %f", i == 0 ? "Left" : "Right", loc.pose.position.x,
//                         loc.pose.position.y, loc.pose.position.z, loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z,
//                         loc.pose.orientation.w);
            }
        }
    }

    void RenderFrame() override {
        CHECK(m_session != XR_NULL_HANDLE)

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
        frameEndInfo.environmentBlendMode = m_preferredBlendMode;
        frameEndInfo.layerCount = (uint32_t) layers.size();
        frameEndInfo.layers = layers.data();
        CHECK_XRCMD(xrEndFrame(m_session, &frameEndInfo))
    }

    bool RenderLayer(XrTime predictedDisplayTime, std::vector<XrCompositionLayerProjectionView> &projectionLayerViews,
                     XrCompositionLayerProjection &layer) {
        XrResult res;

        XrViewState viewState{XR_TYPE_VIEW_STATE};
        auto viewCapacityInput = (uint32_t) m_views.size();
        uint32_t viewCountOutput;

        XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewLocateInfo.displayTime = predictedDisplayTime;
        viewLocateInfo.space = m_appSpace;

        res = xrLocateViews(m_session, &viewLocateInfo, &viewState, viewCapacityInput,
                            &viewCountOutput, m_views.data());
        CHECK_XRRESULT(res, "xrLocateViews")
        if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0 ||
            (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
            return false;
        }

        CHECK(viewCountOutput == viewCapacityInput)
        CHECK(viewCountOutput == m_configViews.size())
        CHECK(viewCountOutput == m_swapchains.size())

        projectionLayerViews.resize(viewCountOutput);

        Quad quad{};

        XrSpaceLocation spaceLocation{XR_TYPE_SPACE_LOCATION};

        // Locate "ViewFront" space
        res = xrLocateSpace(m_visualizedSpaces[0], m_appSpace, predictedDisplayTime, &spaceLocation);
        CHECK_XRRESULT(res, "xrLocateSpace")
        if (XR_UNQUALIFIED_SUCCESS(res)) {
            if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
                (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
                quad = Quad{spaceLocation.pose, {4.45f, 2.5f, 0.0f}};
                //TODO: HMD Pose here!
//                LOG_INFO("HMD pose: pos: %f %f %f ori: %f %f %f %f", spaceLocation.pose.position.x,
//                         spaceLocation.pose.position.y, spaceLocation.pose.position.z, spaceLocation.pose.orientation.x,
//                         spaceLocation.pose.orientation.y, spaceLocation.pose.orientation.z,
//                         spaceLocation.pose.orientation.w);
            }
        } else {
            LOG_INFO("Unable to locate a visualized reference space in app space: %d", res);
        }

        // Render view to the appropriate part of the swapchain image.
        for (uint32_t i = 0; i < viewCountOutput; i++) {
            const Swapchain viewSwapchain = m_swapchains[i];

            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};

            uint32_t swapchainImageIndex;
            CHECK_XRCMD(xrAcquireSwapchainImage(viewSwapchain.handle, &acquireInfo,
                                                &swapchainImageIndex))

            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            CHECK_XRCMD(xrWaitSwapchainImage(viewSwapchain.handle, &waitInfo))

            projectionLayerViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            projectionLayerViews[i].pose = m_views[i].pose;
            projectionLayerViews[i].fov = m_views[i].fov;
            projectionLayerViews[i].subImage.swapchain = viewSwapchain.handle;
            projectionLayerViews[i].subImage.imageRect.offset = {0, 0};
            projectionLayerViews[i].subImage.imageRect.extent = {viewSwapchain.width,
                                                                 viewSwapchain.height};

            const XrSwapchainImageBaseHeader *const swapchainImage = m_swapchainImages[viewSwapchain.handle][swapchainImageIndex];

            m_graphicsPlugin->RenderView(projectionLayerViews[i], swapchainImage,
                                         m_colorSwapchainFormat, quad,
                                         i == 0 ? gstreamerPlayer.getFrameRight().dataHandle
                                                : gstreamerPlayer.getFrameLeft().dataHandle);

            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            CHECK_XRCMD(xrReleaseSwapchainImage(viewSwapchain.handle, &releaseInfo))
        }

        layer.space = m_appSpace;
        layer.layerFlags = m_preferredBlendMode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND
                           ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                             XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT
                           : 0;
        layer.viewCount = (uint32_t) projectionLayerViews.size();
        layer.views = projectionLayerViews.data();

        return true;
    }

private:
    std::shared_ptr<IPlatformPlugin> m_platformPlugin;
    std::shared_ptr<IGraphicsPlugin> m_graphicsPlugin;

    BS::thread_pool threadPool;
    GstreamerPlayer gstreamerPlayer{threadPool};

    XrSpace m_appSpace{XR_NULL_HANDLE};
    XrInstance m_instance{XR_NULL_HANDLE};
    XrSystemId m_systemId{XR_NULL_SYSTEM_ID};
    XrSession m_session{XR_NULL_HANDLE};

    XrEnvironmentBlendMode m_preferredBlendMode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};

    std::vector<XrViewConfigurationView> m_configViews;
    std::vector<Swapchain> m_swapchains;
    std::map<XrSwapchain, std::vector<XrSwapchainImageBaseHeader *>> m_swapchainImages;
    std::vector<XrView> m_views;
    int64_t m_colorSwapchainFormat{-1};

    std::vector<XrSpace> m_visualizedSpaces;
    XrSpace controllerSpace[Side::COUNT];

    XrSessionState m_sessionState{XR_SESSION_STATE_UNKNOWN};
    bool m_sessionRunning{false};

    XrPath handSubactionPath[Side::COUNT];

    XrEventDataBuffer m_eventDataBuffer{};
    InputState m_input;

    const std::set<XrEnvironmentBlendMode> m_acceptableBlendModes;
};

std::shared_ptr<IOpenXrProgram>
CreateOpenXrProgram(const std::shared_ptr<IPlatformPlugin> &platformPlugin,
                    const std::shared_ptr<IGraphicsPlugin> &graphicsPlugin) {
    return std::make_shared<OpenXrProgram>(platformPlugin, graphicsPlugin);
}
