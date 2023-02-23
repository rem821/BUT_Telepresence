#include "gstreamer_player.h"


GstreamerPlayer::GstreamerPlayer(BS::thread_pool &threadPool) {
    threadPool.push_task([&]() {
        gst_init(nullptr, nullptr);
        GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "BUT_Telepresence", 0,
                                "Telepresence system for robots made by BUT");
        gst_debug_set_threshold_for_name("BUT_Telepresence", GST_LEVEL_LOG);

        GstBus *bus;
        GSource *bus_source;
        GError *error = nullptr;
        /* Create our own GLib Main Context and make it the default one */
        context_ = g_main_context_new();
        g_main_context_push_thread_default(context_);

        /* Build pipeline */
        //pipeline_ = gst_parse_launch("rtspsrc location=rtsp://192.168.1.239:8554/left ! application/x-rtp,encoding-name=H264,payload=96 ! rtph264depay ! decodebin ! videoconvert ! video/x-raw,width=1920,height=1080,format=RGBA ! appsink emit-signals=true name=wallesink sync=false", &error);
        //pipeline_ = gst_parse_launch("rtspsrc location=rtsp://192.168.1.239:8556/right ! application/x-rtp,encoding-name=H264,payload=96 ! decodebin ! videoconvert ! video/x-raw,width=1920,height=1080,format=RGBA ! appsink emit-signals=true name=wallesink sync=false", &error);
        //pipeline_ = gst_parse_launch("playbin uri=https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm ! appsink emit-signals=true name=telepresencesink",&error);
        pipeline_ = gst_parse_launch("videotestsrc pattern=ball ! video/x-raw,width=1920,height=1080,format=RGBA ! appsink emit-signals=true name=telepresencesink", &error);

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
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr, nullptr);
        g_source_attach(bus_source, context_);
        g_source_unref(bus_source);
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
        LOG_INFO("GStreamer new frame arrived!");

        GstBuffer *buffer;
        GstMapInfo mapInfo{};

        buffer = gst_sample_get_buffer(sample);
        gst_buffer_map(buffer, &mapInfo, GST_MAP_READ);
        frame->dataHandle = mapInfo.data;
        frame->memorySize = mapInfo.size;

        gst_sample_unref(sample);
        gst_buffer_unmap(buffer, &mapInfo);
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

void GstreamerPlayer::stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
    /* Only pay attention to messages coming from the pipeline, not its children */
    if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
        LOG_INFO("GStreamer state changed to: %s", gst_element_state_get_name(new_state));
    }
}

void GstreamerPlayer::errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_error(msg, &err, &debug_info);

    LOG_ERROR("GStreamer error received from element: %s, %s", GST_OBJECT_NAME(msg->src), err->message);

    gst_element_set_state(pipeline, GST_STATE_NULL);
}
