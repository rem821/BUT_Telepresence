#include "gstreamer_player.h"
#include <ctime>
#include <gst/rtp/gstrtpbuffer.h>


GstreamerPlayer::GstreamerPlayer(BS::thread_pool &threadPool) {
    threadPool.push_task([&]() {
        gst_init(nullptr, nullptr);
        gst_debug_set_threshold_for_name("BUT_Telepresence", GST_LEVEL_TRACE);
        //dumpGstreamerFeatures();

        //Init the CameraFrame data structure
        auto *emptyFrameLeft = new unsigned char[camPair_.first.memorySize];
        memset(emptyFrameLeft, 0, sizeof(emptyFrameLeft));
        camPair_.first.dataHandle = (void *) emptyFrameLeft;

        auto *emptyFrameRight = new unsigned char[camPair_.second.memorySize];
        memset(emptyFrameRight, 0, sizeof(emptyFrameRight));
        camPair_.second.dataHandle = (void *) emptyFrameRight;

        GstBus *bus;
        GSource *bus_source;
        GError *error = nullptr;
        /* Create our own GLib Main Context and make it the default one */
        context_ = g_main_context_new();
        g_main_context_push_thread_default(context_);

        /* Build pipeline */
        pipelineLeft_ = gst_parse_launch(
                "udpsrc port=8554 ! application/x-rtp,encoding-name=JPEG,payload=26 ! identity name=udpsrc_identity_left ! rtpjpegdepay ! jpegdec ! video/x-raw,format=RGB ! queue ! appsink emit-signals=true name=leftsink",
                &error);
        pipelineRight_ = gst_parse_launch(
                "udpsrc port=8556 ! application/x-rtp,encoding-name=JPEG,payload=26 ! identity name=udpsrc_identity_right ! rtpjpegdepay ! jpegdec ! video/x-raw,format=RGB ! queue ! appsink emit-signals=true name=rightsink",
                &error);

        if (error) {
            LOG_ERROR("Unable to build pipeline");
            throw std::runtime_error("Unable to build pipeline!");
        }

        GstElement *leftudpsrc_identity = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "udpsrc_identity_left");
        GstElement *leftappsink = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "leftsink");
        gst_element_set_state(pipelineLeft_, GST_STATE_READY);

        bus = gst_element_get_bus(pipelineLeft_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr, nullptr);
        g_source_attach(bus_source, context_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(leftappsink), "new-sample", (GCallback) newFrameCallbackLeft, &camPair_);
        g_signal_connect(G_OBJECT(leftudpsrc_identity), "handoff", (GCallback) onRtpHeaderMetadata, &camPair_);
        gst_object_unref(bus);

        GstElement *rightudpsrc_identity = gst_bin_get_by_name(GST_BIN(pipelineRight_), "udpsrc_identity_right");
        GstElement *rightappsink = gst_bin_get_by_name(GST_BIN(pipelineRight_), "rightsink");
        gst_element_set_state(pipelineLeft_, GST_STATE_READY);

        bus = gst_element_get_bus(pipelineRight_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr, nullptr);
        g_source_attach(bus_source, context_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(rightappsink), "new-sample", (GCallback) newFrameCallbackRight, &camPair_);
        g_signal_connect(G_OBJECT(rightudpsrc_identity), "handoff", (GCallback) onRtpHeaderMetadata, &camPair_);
        gst_object_unref(bus);

        /* Create a GLib Main Loop and set it to run */
        LOG_INFO("GStreamer entering the main loop");
        mainLoop_ = g_main_loop_new(context_, FALSE);
        play();

        g_main_loop_run(mainLoop_);
        LOG_INFO("GStreamer exited the main loop");
        g_main_loop_unref(mainLoop_);
        mainLoop_ = nullptr;
    });
}

void GstreamerPlayer::play() {
    LOG_INFO("GStreamer setting state to PLAYING");
    gst_element_set_state(pipelineLeft_, GST_STATE_PLAYING);
    gst_element_set_state(pipelineRight_, GST_STATE_PLAYING);
}

GstFlowReturn
GstreamerPlayer::newFrameCallbackLeft(GstElement *sink, CamPair *pair) {
    GstSample *sample;
    /* Retrieve the buffer */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        struct timespec res{};
        clock_gettime(CLOCK_MONOTONIC, &res);
        pair->first.stats.prevTimestamp = pair->first.stats.currTimestamp;
        pair->first.stats.currTimestamp = 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;

        GstBuffer *buffer;
        GstMapInfo mapInfo{};

        buffer = gst_sample_get_buffer(sample);
        gst_buffer_map(buffer, &mapInfo, GST_MAP_READ);

        memcpy(pair->first.dataHandle, mapInfo.data, pair->first.memorySize);

        gst_sample_unref(sample);
        gst_buffer_unmap(buffer, &mapInfo);
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

GstFlowReturn
GstreamerPlayer::newFrameCallbackRight(GstElement *sink, CamPair *pair) {
    GstSample *sample;
    /* Retrieve the buffer */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        struct timespec res{};
        clock_gettime(CLOCK_MONOTONIC, &res);
        pair->second.stats.prevTimestamp = pair->second.stats.currTimestamp;
        pair->second.stats.currTimestamp = 1000.0 * res.tv_sec + (double) res.tv_nsec / 1e6;

        GstBuffer *buffer;
        GstMapInfo mapInfo{};

        buffer = gst_sample_get_buffer(sample);
        gst_buffer_map(buffer, &mapInfo, GST_MAP_READ);

        memcpy(pair->second.dataHandle, mapInfo.data, pair->second.memorySize);

        gst_sample_unref(sample);
        gst_buffer_unmap(buffer, &mapInfo);
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

void GstreamerPlayer::onRtpHeaderMetadata(GstElement *identity, GstBuffer *buffer, gpointer data) {
    auto* pair = reinterpret_cast<CamPair*>(data);
    bool isLeftCamera = std::string(identity->object.name) == "rtpjpegdepay_identity_left";
    auto frame = isLeftCamera ? pair->first : pair->second;
    frame.stats.totalLatency = 0;

    GstRTPBuffer rtp_buf = GST_RTP_BUFFER_INIT;
    gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp_buf);
    gpointer myInfoBuf = nullptr;
    guint size_64 = 8;
    guint8 appbits = 1;
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 0, &myInfoBuf, &size_64) != 0) {
        if (isLeftCamera) {
            LOG_INFO("left frame id: %lu", *(static_cast<uint64_t *>(myInfoBuf)));
        } else {
            LOG_INFO("right frame id: %lu", *(static_cast<uint64_t *>(myInfoBuf)));
        }
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 1, &myInfoBuf, &size_64) != 0) {
       frame.stats.nvvidconv = *(static_cast<uint64_t *>(myInfoBuf));
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 2, &myInfoBuf, &size_64) != 0) {
        frame.stats.jpegenc = *(static_cast<uint64_t *>(myInfoBuf));
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 3, &myInfoBuf, &size_64) != 0) {
        frame.stats.rtpjpegpay = *(static_cast<uint64_t *>(myInfoBuf));
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 4, &myInfoBuf, &size_64) != 0) {
        frame.stats.rtpjpegpayTimestamp = *(static_cast<uint64_t *>(myInfoBuf));
    }
    gst_rtp_buffer_unmap(&rtp_buf);
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