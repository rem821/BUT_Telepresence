#include "gstreamer_player.h"
#include <ctime>
#include <gst/rtp/rtp.h>


GstreamerPlayer::GstreamerPlayer(BS::thread_pool &threadPool) {
    threadPool.push_task([&]() {
        gst_init(nullptr, nullptr);
        gst_debug_set_threshold_for_name("BUT_Telepresence", GST_LEVEL_TRACE);
        //dumpGstreamerFeatures();

        //Init the CameraFrame data structure
        camPair_ = new CamPair();
        camPair_->first.stats = new CameraStats();
        camPair_->second.stats = new CameraStats();

        auto *emptyFrameLeft = new unsigned char[camPair_->first.memorySize];
        memset(emptyFrameLeft, 0, sizeof(emptyFrameLeft));
        camPair_->first.dataHandle = (void *) emptyFrameLeft;

        auto *emptyFrameRight = new unsigned char[camPair_->second.memorySize];
        memset(emptyFrameRight, 0, sizeof(emptyFrameRight));
        camPair_->second.dataHandle = (void *) emptyFrameRight;

        GstBus *bus;
        GSource *bus_source;
        GError *error = nullptr;
        /* Create our own GLib Main Context and make it the default one */
        context_ = g_main_context_new();
        g_main_context_push_thread_default(context_);

        /* Build pipeline */
        pipelineLeft_ = gst_parse_launch(
                "udpsrc port=8554 ! application/x-rtp,encoding-name=JPEG,payload=26"
                " ! identity name=udpsrc_identity"
                " ! rtpjpegdepay ! identity name=rtpjpegdepay_identity"
                " ! jpegdec ! video/x-raw,format=RGB ! identity name=jpegdec_identity"
                " ! queue ! identity name=queue_identity"
                " ! appsink emit-signals=true name=appsink sync=false",
                &error);
        pipelineRight_ = gst_parse_launch(
                "udpsrc port=8556 ! application/x-rtp,encoding-name=JPEG,payload=26"
                " ! identity name=udpsrc_identity"
                " ! rtpjpegdepay ! identity name=rtpjpegdepay_identity"
                " ! jpegdec ! video/x-raw,format=RGB ! identity name=jpegdec_identity"
                " ! queue ! identity name=queue_identity"
                " ! appsink emit-signals=true name=appsink sync=false",
                &error);

        if (error) {
            LOG_ERROR("Unable to build pipeline");
            throw std::runtime_error("Unable to build pipeline!");
        }

        GstElement *leftudpsrc_identity = gst_bin_get_by_name(GST_BIN(pipelineLeft_),
                                                              "udpsrc_identity");
        GstElement *leftrtpjpegdepay_identity = gst_bin_get_by_name(GST_BIN(pipelineLeft_),
                                                                    "rtpjpegdepay_identity");
        GstElement *leftjpegdec_identity = gst_bin_get_by_name(GST_BIN(pipelineLeft_),
                                                               "jpegdec_identity");
        GstElement *leftqueue_identity = gst_bin_get_by_name(GST_BIN(pipelineLeft_),
                                                             "queue_identity");
        GstElement *leftappsink = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "appsink");
        gst_element_set_name(pipelineLeft_, "pipeline_left");
        gst_element_set_state(pipelineLeft_, GST_STATE_READY);

        bus = gst_element_get_bus(pipelineLeft_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr,
                              nullptr);
        g_source_attach(bus_source, context_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback,
                         pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback,
                         pipelineLeft_);
        g_signal_connect(G_OBJECT(leftappsink), "new-sample", (GCallback) newFrameCallback,
                         camPair_);
        //g_signal_connect(G_OBJECT(leftudpsrc_identity), "handoff", (GCallback) onRtpHeaderMetadata,
        //                 camPair_);
        g_signal_connect(G_OBJECT(leftrtpjpegdepay_identity), "handoff",
                         (GCallback) onIdentityHandoff, camPair_);
        g_signal_connect(G_OBJECT(leftjpegdec_identity), "handoff", (GCallback) onIdentityHandoff,
                         camPair_);
        g_signal_connect(G_OBJECT(leftqueue_identity), "handoff", (GCallback) onIdentityHandoff,
                         camPair_);
        gst_object_unref(leftudpsrc_identity);
        gst_object_unref(leftrtpjpegdepay_identity);
        gst_object_unref(leftjpegdec_identity);
        gst_object_unref(leftqueue_identity);
        gst_object_unref(leftappsink);
        gst_object_unref(bus);

        GstElement *rightudpsrc_identity = gst_bin_get_by_name(GST_BIN(pipelineRight_),
                                                               "udpsrc_identity");
        GstElement *rightrtpjpegdepay_identity = gst_bin_get_by_name(GST_BIN(pipelineRight_),
                                                                     "rtpjpegdepay_identity");
        GstElement *rightjpegdec_identity = gst_bin_get_by_name(GST_BIN(pipelineRight_),
                                                                "jpegdec_identity");
        GstElement *rightqueue_identity = gst_bin_get_by_name(GST_BIN(pipelineRight_),
                                                              "queue_identity");
        GstElement *rightappsink = gst_bin_get_by_name(GST_BIN(pipelineRight_), "appsink");
        gst_element_set_name(pipelineRight_, "pipeline_right");
        gst_element_set_state(pipelineRight_, GST_STATE_READY);

        bus = gst_element_get_bus(pipelineRight_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr,
                              nullptr);
        g_source_attach(bus_source, context_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback,
                         pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback,
                         pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback,
                         pipelineRight_);
        g_signal_connect(G_OBJECT(rightappsink), "new-sample", (GCallback) newFrameCallback,
                         camPair_);
        //g_signal_connect(G_OBJECT(rightudpsrc_identity), "handoff", (GCallback) onRtpHeaderMetadata,
        //                 camPair_);
        g_signal_connect(G_OBJECT(rightrtpjpegdepay_identity), "handoff",
                         (GCallback) onIdentityHandoff, camPair_);
        g_signal_connect(G_OBJECT(rightjpegdec_identity), "handoff", (GCallback) onIdentityHandoff,
                         camPair_);
        g_signal_connect(G_OBJECT(rightqueue_identity), "handoff", (GCallback) onIdentityHandoff,
                         camPair_);
        gst_object_unref(rightudpsrc_identity);
        gst_object_unref(rightrtpjpegdepay_identity);
        gst_object_unref(rightjpegdec_identity);
        gst_object_unref(rightqueue_identity);
        gst_object_unref(rightappsink);
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
GstreamerPlayer::newFrameCallback(GstElement *sink, CamPair *pair) {
    GstSample *sample;
    /* Retrieve the buffer */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        bool isLeftCamera = std::string(sink->object.parent->name) == "pipeline_left";
        auto frame = isLeftCamera ? pair->first : pair->second;

        frame.stats->prevTimestamp = frame.stats->currTimestamp;
        frame.stats->currTimestamp = getCurrentUs();
        double diff = frame.stats->currTimestamp - frame.stats->prevTimestamp;
        frame.stats->fps = 1e6f / diff;

        GstBuffer *buffer;
        GstMapInfo mapInfo{};

        buffer = gst_sample_get_buffer(sample);
        gst_buffer_map(buffer, &mapInfo, GST_MAP_READ);

        memcpy(frame.dataHandle, mapInfo.data, frame.memorySize);

        gst_sample_unref(sample);
        gst_buffer_unmap(buffer, &mapInfo);
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

//void GstreamerPlayer::onRtpHeaderMetadata(GstElement *identity, GstBuffer *buffer, gpointer data) {
//    auto *pair = reinterpret_cast<CamPair *>(data);
//    bool isLeftCamera = std::string(identity->object.parent->name) == "pipeline_left";
//    auto stats = isLeftCamera ? pair->first.stats : pair->second.stats;
//    stats->totalLatency = 0;
//
//    GstRTPBuffer rtp_buf = GST_RTP_BUFFER_INIT;
//    gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp_buf);
//    gpointer myInfoBuf = nullptr;
//    guint size_64 = 8;
//    guint8 appbits = 1;
//    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 0, &myInfoBuf,
//                                                     &size_64) != 0) {
//        stats->frameId = *(static_cast<uint64_t *>(myInfoBuf));
//    }
//    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 1, &myInfoBuf,
//                                                     &size_64) != 0) {
//        stats->nvvidconv = *(static_cast<uint64_t *>(myInfoBuf));
//    }
//    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 2, &myInfoBuf,
//                                                     &size_64) != 0) {
//        stats->jpegenc = *(static_cast<uint64_t *>(myInfoBuf));
//    }
//    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 3, &myInfoBuf,
//                                                     &size_64) != 0) {
//        stats->rtpjpegpay = *(static_cast<uint64_t *>(myInfoBuf));
//    }
//    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 4, &myInfoBuf,
//                                                     &size_64) != 0) {
//        stats->rtpjpegpayTimestamp = *(static_cast<uint64_t *>(myInfoBuf));
//    }
//    gst_rtp_buffer_unmap(&rtp_buf);
//
//    // This is so the last packet of rtp gets saved
//    stats->udpsrcTimestamp = getCurrentUs();
//    stats->udpstream = stats->udpsrcTimestamp - stats->rtpjpegpayTimestamp;
//}

void GstreamerPlayer::onIdentityHandoff(GstElement *identity, GstBuffer *buffer, gpointer data) {
    auto *pair = reinterpret_cast<CamPair *>(data);
    bool isLeftCamera = std::string(identity->object.parent->name) == "pipeline_left";
    auto *stats = isLeftCamera ? pair->first.stats : pair->second.stats;

    if (std::string(identity->object.name) == "rtpjpegdepay_identity") {
        stats->rtpjpegdepayTimestamp = getCurrentUs();
        stats->rtpjpegdepay = stats->rtpjpegdepayTimestamp - stats->udpsrcTimestamp;

    } else if (std::string(identity->object.name) == "jpegdec_identity") {
        stats->jpegdecTimestamp = getCurrentUs();
        stats->jpegdec = stats->jpegdecTimestamp - stats->rtpjpegdepayTimestamp;

    } else if (std::string(identity->object.name) == "queue_identity") {
        stats->queueTimestamp = getCurrentUs();
        stats->queue = stats->queueTimestamp - stats->jpegdecTimestamp;
        stats->totalLatency = stats->nvvidconv + stats->jpegenc + stats->rtpjpegpay +
                              stats->udpstream + stats->rtpjpegdepay +
                              stats->jpegdec + stats->queue;
        LOG_INFO(
                "Pipeline latencies: nvvidconv: %lu, jpegenc: %lu, rtpjpegpay: %lu, udpstream: %lu, rtpjpegdepay: %lu, jpegdec: %lu, queue: %lu, total: %lu",
                (unsigned long) stats->nvvidconv, (unsigned long) stats->jpegenc,
                (unsigned long) stats->rtpjpegpay, (unsigned long) stats->udpstream,
                (unsigned long) stats->rtpjpegdepay, (unsigned long) stats->jpegdec,
                (unsigned long) stats->queue,
                (unsigned long) stats->totalLatency);
    }
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

uint64_t GstreamerPlayer::getCurrentUs() {
    struct timespec res{};
    clock_gettime(CLOCK_REALTIME, &res);
    int64_t us = 1e6 * res.tv_sec + (int64_t) res.tv_nsec / 1e3;
    return us;
}