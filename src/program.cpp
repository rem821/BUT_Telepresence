#include "pch.h"
#include "log.h"
#include "check.h"
#include "graphics_plugin.h"
#include "platform_plugin.h"
#include "program.h"

inline std::string GetXrVersionString(XrVersion ver) {
    return Fmt("%d.%d.%d", XR_VERSION_MAJOR(ver), XR_VERSION_MINOR(ver), XR_VERSION_PATCH(ver));
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

        LOG_INFO("Using system %u for form factor %s", m_systemId,
                 to_string(XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY));
        CHECK(m_instance != XR_NULL_HANDLE);
        CHECK(m_systemId != XR_NULL_SYSTEM_ID);

        UpdatePreferredBlendMode();
    }

    void InitializeDevice() override {
        LogViewConfigurations();
    }

private:
    std::shared_ptr<IPlatformPlugin> m_platformPlugin;
    std::shared_ptr<IGraphicsPlugin> m_graphicsPlugin;

    XrInstance m_instance{XR_NULL_HANDLE};
    XrSystemId m_systemId{XR_NULL_SYSTEM_ID};

    XrEnvironmentBlendMode m_preferredBlendMode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};

    const std::set<XrEnvironmentBlendMode> m_acceptableBlendModes;
};

std::shared_ptr<IOpenXrProgram>
CreateOpenXrProgram(const std::shared_ptr<IPlatformPlugin> &platformPlugin,
                    const std::shared_ptr<IGraphicsPlugin> &graphicsPlugin) {
    return std::make_shared<OpenXrProgram>(platformPlugin, graphicsPlugin);
}
