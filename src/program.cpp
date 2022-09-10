#include "pch.h"
#include "log.h"
#include "check.h"
#include "graphics_plugin.h"
#include "platform_plugin.h"
#include "program.h"

namespace Math {
    namespace Pose {
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
        referenceSpaceCreateInfo.poseInReferenceSpace
                = Math::Pose::RotateCCWAboutYAxis(0.f, {-2.f, 0.f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRight")) {
        referenceSpaceCreateInfo.poseInReferenceSpace
                = Math::Pose::RotateCCWAboutYAxis(0.f, {2.f, 0.f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeftRotated")) {
        referenceSpaceCreateInfo.poseInReferenceSpace
                = Math::Pose::RotateCCWAboutYAxis(3.14f / 3.f, {-2.f, 0.5f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRightRotated")) {
        referenceSpaceCreateInfo.poseInReferenceSpace
                = Math::Pose::RotateCCWAboutYAxis(-3.14f / 3.f, {2.f, 0.5f, -2.f});
        referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else {
        throw std::invalid_argument(
                Fmt("Unknown reference space type '%s'", referenceSpaceTypeStr.c_str()));
    }
    return referenceSpaceCreateInfo;
}


struct OpenXrProgram : IOpenXrProgram {
    OpenXrProgram(const std::shared_ptr<IPlatformPlugin> &platformPlugin,
                  const std::shared_ptr<IGraphicsPlugin> &graphicsPlugin)
            : m_platformPlugin(platformPlugin),
              m_graphicsPlugin(graphicsPlugin),
              m_acceptableBlendModes{XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
                                     XR_ENVIRONMENT_BLEND_MODE_ADDITIVE,
                                     XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND} {}

    ~OpenXrProgram() override {}

    static void LogLayersAndExtensions() {
        const auto logExtensions = [](const char *layerName, int indent = 0) {
            uint32_t instanceExtensionCount;
            CHECK_XRCMD(
                    xrEnumerateInstanceExtensionProperties(layerName, 0, &instanceExtensionCount,
                                                           nullptr));

            std::vector<XrExtensionProperties> extensions(instanceExtensionCount);
            for (XrExtensionProperties &extension: extensions) {
                extension.type = XR_TYPE_EXTENSION_PROPERTIES;
            }

            CHECK_XRCMD(
                    xrEnumerateInstanceExtensionProperties(layerName, (uint32_t) extensions.size(),
                                                           &instanceExtensionCount,
                                                           extensions.data()));

            const std::string indentStr(indent, ' ');
            LOG_INFO("%sAvailable Extensions: (%d)", indentStr.c_str(), instanceExtensionCount);
            for (const XrExtensionProperties &extension: extensions) {
                LOG_INFO("%s  Name=%s SpecVersion=%d", indentStr.c_str(), extension.extensionName,
                         extension.extensionVersion);
            }
        };

        // Log non-layer extensions (layerName==nullptr).
        logExtensions(nullptr);

        // Log layers and any of their extensions.
        {
            uint32_t layerCount;
            CHECK_XRCMD(xrEnumerateApiLayerProperties(0, &layerCount, nullptr));

            std::vector<XrApiLayerProperties> layers(layerCount);
            for (XrApiLayerProperties &layer: layers) {
                layer.type = XR_TYPE_API_LAYER_PROPERTIES;
            }

            CHECK_XRCMD(xrEnumerateApiLayerProperties((uint32_t) layers.size(), &layerCount,
                                                      layers.data()));

            LOG_INFO("Available Layers: (%d)", layerCount);
            for (const XrApiLayerProperties &layer: layers) {
                LOG_INFO(
                        "  Name=%s SpecVersion=%s LayerVersion=%d Description=%s",
                        layer.layerName,
                        GetXrVersionString(layer.specVersion).c_str(), layer.layerVersion,
                        layer.description);
                logExtensions(layer.layerName, 4);
            }
        }
    }

    void LogInstanceInfo() {
        CHECK(m_instance != XR_NULL_HANDLE);

        XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
        CHECK_XRCMD(xrGetInstanceProperties(m_instance, &instanceProperties));

        LOG_INFO("Instance RuntimeName=%s RuntimeVersion=%s", instanceProperties.runtimeName,
                 GetXrVersionString(instanceProperties.runtimeVersion).c_str());
    }

    void CreateInstanceInternal() {
        CHECK(m_instance == XR_NULL_HANDLE);

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

        strcpy(createInfo.applicationInfo.applicationName, "HelloXR");
        createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

        CHECK_XRCMD(xrCreateInstance(&createInfo, &m_instance));
    }

    void CreateInstance() override {
        LogLayersAndExtensions();

        CreateInstanceInternal();

        LogInstanceInfo();
    }

    void LogViewConfigurations() {
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId != XR_NULL_SYSTEM_ID);

        uint32_t viewConfigTypeCount;
        CHECK_XRCMD(xrEnumerateViewConfigurations(m_instance, m_systemId, 0, &viewConfigTypeCount,
                                                  nullptr));
        std::vector<XrViewConfigurationType> viewConfigTypes(viewConfigTypeCount);
        CHECK_XRCMD(xrEnumerateViewConfigurations(m_instance, m_systemId, viewConfigTypeCount,
                                                  &viewConfigTypeCount,
                                                  viewConfigTypes.data()));
        CHECK((uint32_t) viewConfigTypes.size() == viewConfigTypeCount);

        LOG_INFO("Available View Configuration Types: (%d)", viewConfigTypeCount);
        for (auto viewConfigType: viewConfigTypes) {
            LOG_INFO("  View Configuration Type: %s %s", to_string(viewConfigType),
                     viewConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ? "(Selected)"
                                                                                 : "");

            XrViewConfigurationProperties viewConfigProperties{
                    XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
            CHECK_XRCMD(xrGetViewConfigurationProperties(m_instance, m_systemId, viewConfigType,
                                                         &viewConfigProperties));

            LOG_INFO("  View Configuration FovMutable=%s",
                     viewConfigProperties.fovMutable == XR_TRUE ? "True" : "False");

            uint32_t viewCount;
            CHECK_XRCMD(xrEnumerateViewConfigurationViews(m_instance, m_systemId, viewConfigType, 0,
                                                          &viewCount,
                                                          nullptr));
            if (viewCount > 0) {
                std::vector<XrViewConfigurationView> views(viewCount,
                                                           {XR_TYPE_VIEW_CONFIGURATION_VIEW});
                CHECK_XRCMD(
                        xrEnumerateViewConfigurationViews(m_instance, m_systemId, viewConfigType,
                                                          viewCount,
                                                          &viewCount,
                                                          views.data()));

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
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId != XR_NULL_SYSTEM_ID);

        uint32_t count;
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId, type, 0, &count,
                                                     nullptr));
        CHECK(count > 0);

        LOG_INFO("Available Environment Blend Mode count: (%u)", count);

        std::vector<XrEnvironmentBlendMode> blendModes(count);
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId, type, count, &count,
                                                     blendModes.data()));
        bool blendModeFound = false;
        for (auto mode: blendModes) {
            const bool blendModeMatch = (mode == m_preferredBlendMode);
            LOG_INFO("Environment Blend Mode (%s): %s", to_string(mode),
                     blendModeMatch ? "(Selected)" : "");
            blendModeFound |= blendModeMatch;
        }
        CHECK(blendModeFound);
    }

    XrEnvironmentBlendMode GetPreferredBlendMode() const override {
        return m_preferredBlendMode;
    }

    void UpdatePreferredBlendMode() {
        uint32_t count;
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId,
                                                     XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                     &count,
                                                     nullptr));
        CHECK(count > 0);

        std::vector<XrEnvironmentBlendMode> blendModes(count);
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance, m_systemId,
                                                     XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                     count, &count, blendModes.data()));
        for (const auto &blendMode: blendModes) {
            if (m_acceptableBlendModes.count(blendMode)) {
                LOG_INFO("Preferred Blend Mode: %s", to_string(blendMode));
                m_preferredBlendMode = blendMode;
                return;
            }
        }
        THROW("No acceptable blend mode returned from the xrEnumerateEnvironmentBlendModes");
    }

    void InitializeSystem() override {
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId == XR_NULL_SYSTEM_ID);

        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        CHECK_XRCMD(xrGetSystem(m_instance, &systemInfo, &m_systemId));

        LOG_INFO("Using system %llu for form factor %s", (unsigned long long) m_systemId,
                 to_string(XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY));
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId != XR_NULL_SYSTEM_ID);

        UpdatePreferredBlendMode();
    }

    void InitializeDevice() override {
        LogViewConfigurations();

        m_graphicsPlugin->InitializeDevice(m_instance, m_systemId);
    }

    void LogReferenceSpaces() {
        CHECK(m_session != XR_NULL_HANDLE);

        uint32_t spaceCount;
        CHECK_XRCMD(xrEnumerateReferenceSpaces(m_session, 0, &spaceCount, nullptr));
        std::vector<XrReferenceSpaceType> spaces(spaceCount);
        CHECK_XRCMD(xrEnumerateReferenceSpaces(m_session, spaceCount, &spaceCount, spaces.data()));

        LOG_INFO("Available reference spaces: %d", spaceCount);
        for (auto space: spaces) {
            LOG_INFO("  Name: %s", to_string(space));
        }
    }

    struct InputState {
        XrActionSet actionSet{XR_NULL_HANDLE};
    };

    void InitializeActions() {
        // Create an action set
        {
            XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
            strcpy(actionSetInfo.actionSetName, "gameplay");
            strcpy(actionSetInfo.localizedActionSetName, "Gameplay");
            actionSetInfo.priority = 0;
            CHECK_XRCMD(xrCreateActionSet(m_instance, &actionSetInfo, &m_input.actionSet));
        }

        // Bind actions
    }

    void CreateVisualizedSpaces() {
        CHECK(m_session != XR_NULL_HANDLE);

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
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_session == XR_NULL_HANDLE);

        {
            LOG_INFO("Creating session...");
            XrSessionCreateInfo createInfo{XR_TYPE_SESSION_CREATE_INFO};
            createInfo.next = m_graphicsPlugin->GetGraphicsBinding();
            createInfo.systemId = m_systemId;
            CHECK_XRCMD(xrCreateSession(m_instance, &createInfo, &m_session));
        }

        LogReferenceSpaces();
        InitializeActions();
        CreateVisualizedSpaces();

        {
            XrReferenceSpaceCreateInfo referenceSpaceCreateInfo
                    = GetXrReferenceSpaceCreateInfo("Local");
            CHECK_XRCMD(xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &m_appSpace));
        }
    }

    void CreateSwapchains() override {
        CHECK(m_session != XR_NULL_HANDLE);
        CHECK(m_swapchains.empty());
        CHECK(m_configViews.empty());

        // REad graphics properties for preferred swapchain length and logging
        XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
        CHECK_XRCMD(xrGetSystemProperties(m_instance, m_systemId, &systemProperties));

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
                0, &viewCount, nullptr));
        m_configViews.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        CHECK_XRCMD(xrEnumerateViewConfigurationViews(
                m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                viewCount, &viewCount, m_configViews.data()));


        // Create and cache view buffer for xrLocateViews later.
        m_views.resize(viewCount, {XR_TYPE_VIEW});

        if (viewCount > 0) {
            uint32_t swapchainFormatCount;
            CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session, 0, &swapchainFormatCount, nullptr));
            std::vector<int64_t> swapchainFormats(swapchainFormatCount);
            CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session, swapchainFormatCount,
                                                    &swapchainFormatCount,
                                                    swapchainFormats.data()));
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
                Swapchain swapchain;
                swapchain.width = swapchainCreateInfo.width;
                swapchain.height = swapchainCreateInfo.height;
                CHECK_XRCMD(xrCreateSwapchain(m_session, &swapchainCreateInfo, &swapchain.handle));

                m_swapchains.push_back(swapchain);
                uint32_t imageCount;
                CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr));
                std::vector<XrSwapchainImageBaseHeader *> swapchainImages =
                        m_graphicsPlugin->AllocateSwapchainImageStructs(imageCount,
                                                                        swapchainCreateInfo);
                CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount,
                                                       swapchainImages[0]));
                m_swapchainImages.insert(
                        std::make_pair(swapchain.handle, std::move(swapchainImages)));
            }
        }
    }

private:
    std::shared_ptr<IPlatformPlugin> m_platformPlugin;
    std::shared_ptr<IGraphicsPlugin> m_graphicsPlugin;

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

    InputState m_input;

    const std::set<XrEnvironmentBlendMode> m_acceptableBlendModes;
};

std::shared_ptr<IOpenXrProgram>
CreateOpenXrProgram(const std::shared_ptr<IPlatformPlugin> &platformPlugin,
                    const std::shared_ptr<IGraphicsPlugin> &graphicsPlugin) {
    return std::make_shared<OpenXrProgram>(platformPlugin, graphicsPlugin);
}
