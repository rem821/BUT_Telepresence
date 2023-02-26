#include "gstreamer_player.h"


GstreamerPlayer::GstreamerPlayer(BS::thread_pool &threadPool) {
    threadPool.push_task([&]() {
        gst_init(nullptr, nullptr);
        gst_debug_set_threshold_for_name("BUT_Telepresence", GST_LEVEL_LOG);
        //dumpGstreamerFeatures();

        GstBus *bus;
        GSource *bus_source;
        GError *error = nullptr;
        /* Create our own GLib Main Context and make it the default one */
        context_ = g_main_context_new();
        g_main_context_push_thread_default(context_);

        /* Build pipeline */
        pipeline_ = gst_parse_launch(
                "rtspsrc location=rtsp://192.168.1.239:8554/left latency=0 ! application/x-rtp,encoding-name=H264 ! decodebin3 ! video/x-raw,width=1920,height=1080,format=I420 ! appsink emit-signals=true name=telepresencesink",
                &error);
        //pipeline_ = gst_parse_launch(
        //        "rtspsrc location=rtsp://192.168.1.239:8556/right latency=0 ! application/x-rtp,encoding-name=H264 ! decodebin3 ! video/x-raw,width=1920,height=1080,format=I420 ! appsink emit-signals=true name=telepresencesink",
        //        &error);
        //pipeline_ = gst_parse_launch("videotestsrc pattern=snow ! video/x-raw,width=1920,height=1080,format=RGBA ! appsink emit-signals=true name=telepresencesink", &error);

        if (error) {
            LOG_ERROR("Unable to build pipeline");
            throw std::runtime_error("Unable to build pipeline!");
        }

        GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline_), "telepresencesink");
        /* Set the pipeline to READY, so it can already accept a window handle, if we have one */
        gst_element_set_state(pipeline_, GST_STATE_READY);

        /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
        bus = gst_element_get_bus(pipeline_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr,
                              nullptr);
        g_source_attach(bus_source, context_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipeline_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback, pipeline_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback, pipeline_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback,
                         pipeline_);
        g_signal_connect(G_OBJECT(appsink), "new-sample", (GCallback) newFrameCallback,
                         &gstreamerFrame_);
        gst_object_unref(bus);

        /* Create a GLib Main Loop and set it to run */
        LOG_INFO("GStreamer entering the main loop");
        mainLoop_ = g_main_loop_new(context_, FALSE);
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);

        g_main_loop_run(mainLoop_);
        LOG_INFO("GStreamer exited the main loop");
        g_main_loop_unref(mainLoop_);
        mainLoop_ = nullptr;
    });
}

void GstreamerPlayer::play() {
    LOG_INFO("GStreamer setting state to PLAYING");
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
}

GstFlowReturn GstreamerPlayer::newFrameCallback(GstElement *sink, GstreamerFrame *frame) {
    GstSample *sample;
    /* Retrieve the buffer */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        //LOG_INFO("GStreamer new frame arrived!");

        GstBuffer *buffer;
        GstMapInfo mapInfo{};

        buffer = gst_sample_get_buffer(sample);
        gst_buffer_map(buffer, &mapInfo, GST_MAP_READ);
        frame->dataHandle = mapInfo.data; //convertImage(mapInfo.data);
        frame->memorySize = mapInfo.size * 2;

        gst_sample_unref(sample);
        gst_buffer_unmap(buffer, &mapInfo);
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

void GstreamerPlayer::stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

    LOG_INFO("GStreamer element %s state changed to: %s", GST_MESSAGE_SRC(msg)->name,
             gst_element_state_get_name(new_state));
}

void GstreamerPlayer::infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_info(msg, &err, &debug_info);

    LOG_INFO("GStreamer info received from element: %s, %s", GST_OBJECT_NAME(msg->src),
             err->message);

    gst_element_set_state(pipeline, GST_STATE_NULL);
}

void GstreamerPlayer::warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_warning(msg, &err, &debug_info);

    LOG_INFO("GStreamer warning received from element: %s, %s", GST_OBJECT_NAME(msg->src),
             err->message);

    gst_element_set_state(pipeline, GST_STATE_NULL);
}

void GstreamerPlayer::errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_error(msg, &err, &debug_info);

    LOG_ERROR("GStreamer error received from element: %s, %s", GST_OBJECT_NAME(msg->src),
              err->message);

    gst_element_set_state(pipeline, GST_STATE_NULL);
}

void GstreamerPlayer::dumpGstreamerFeatures() {
    GstRegistry *registry = gst_registry_get();

    if (!registry) {
        LOG_ERROR("Failed to get gstreamer registry!");
        throw std::runtime_error("Failed to get gstreamer registry!");
    }

    gchar *str = g_strdup("");
    GList *features = gst_registry_feature_filter(registry, nullptr, FALSE, nullptr);
    for (GList *iterator = features; iterator != nullptr; iterator = iterator->next) {
        printGstreamerFeature((GstPluginFeature *) iterator->data, &str);
    }

    g_list_free(features);

    gst_object_unref(registry);
}

gboolean
GstreamerPlayer::printGstreamerFeature(const GstPluginFeature *feature, gpointer user_data) {
    auto **str = (gchar **) user_data;
    gchar *name = gst_plugin_feature_get_name(feature);
    gchar *temp;

    /* Get the plugin name if this is a plugin feature */
    gchar *plugin_name = nullptr;
    if (GST_IS_PLUGIN_FEATURE(feature)) {
        GstPlugin *plugin = gst_plugin_feature_get_plugin((GstPluginFeature *) feature);
        if (plugin != nullptr) {
            plugin_name = g_strdup(gst_plugin_get_name(plugin));
            gst_object_unref(plugin);
        }
    }

    /* Append the feature name and plugin name (if any) to the string */
    if (plugin_name != nullptr) {
        LOG_INFO("GStreamer found feature from plugin: %s (%s)\n", name, plugin_name);
        temp = g_strdup_printf("%s (%s)\n", name, plugin_name);
    } else {
        LOG_INFO("GStreamer found feature: %s \n", name);
        temp = g_strdup_printf("%s\n", name);
    }
    *str = g_strdup_printf("%s%s", *str, temp);
    g_free(temp);

    /* Free resources */
    g_free(name);
    g_free(plugin_name);

    return TRUE;
}

void *GstreamerPlayer::YUV420toRGB(void *image) {
    int width = 1920;
    int height = 1080;
    auto *rgb_image = new unsigned char[width * height *
                                        3]; //width and height of the image to be converted
    const unsigned char *yuv_image = (unsigned char *) image;
    const unsigned char *y_plane = yuv_image;
    const unsigned char *u_plane = yuv_image + width * height;
    const unsigned char *v_plane = yuv_image + width * height * 5 / 4;

    int uv_stride = width / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 2) {
            int y0 = y_plane[y * width + x];
            int y1 = y_plane[y * width + x + 1];
            int u = u_plane[(y / 2) * uv_stride + (x / 2)];
            int v = v_plane[(y / 2) * uv_stride + (x / 2)];

            int r0 = y0 + 1.370705f * (v - 128);
            int g0 = y0 - 0.698001f * (v - 128) - 0.337633f * (u - 128);
            int b0 = y0 + 1.732446f * (u - 128);

            int r1 = y1 + 1.370705f * (v - 128);
            int g1 = y1 - 0.698001f * (v - 128) - 0.337633f * (u - 128);
            int b1 = y1 + 1.732446f * (u - 128);

            r0 = std::min(255, std::max(0, r0));
            g0 = std::min(255, std::max(0, g0));
            b0 = std::min(255, std::max(0, b0));

            r1 = std::min(255, std::max(0, r1));
            g1 = std::min(255, std::max(0, g1));
            b1 = std::min(255, std::max(0, b1));

            rgb_image[(y * width + x) * 3] = r0;
            rgb_image[(y * width + x) * 3 + 1] = g0;
            rgb_image[(y * width + x) * 3 + 2] = b0;

            rgb_image[(y * width + x + 1) * 3] = r1;
            rgb_image[(y * width + x + 1) * 3 + 1] = g1;
            rgb_image[(y * width + x + 1) * 3 + 2] = b1;
        }
    }

    return (void *) rgb_image;
}