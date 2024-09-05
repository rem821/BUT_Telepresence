#include "pch.h"
#include <gst/gst.h>
#include <gio/gio.h>
#include "log.h"
#include "common.h"
#include "BS_thread_pool.hpp"

#pragma once


class GstreamerPlayer {
public:


    explicit GstreamerPlayer(CamPair* camPair);

    ~GstreamerPlayer() = default;

    void configurePipeline(BS::thread_pool &threadPool, const StreamingConfig& config);

    void playPipelines();

    void stopPipelines();

private:

    static GstFlowReturn newFrameCallback(GstElement *sink, CamPair *pair);

    static void onRtpHeaderMetadata(GstElement *identity, GstBuffer *buffer, gpointer data);

    static void onIdentityHandoff(GstElement *identity, GstBuffer *buffer, gpointer data);

    static void stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static uint64_t getCurrentUs();

    void dumpGstreamerFeatures();

    gboolean printGstreamerFeature(const GstPluginFeature *feature, gpointer user_data);

    GstElement *pipelineLeft_{}, *pipelineRight_{};
    GMainContext *context_{};
    GMainLoop *mainLoop_{};

    CamPair* camPair_;

    const std::string jpegPipeline_ = "udpsrc name=udpsrc ! application/x-rtp,encoding-name=JPEG,payload=26 ! identity name=udpsrc_identity ! rtpjpegdepay ! identity name=rtpjpegdepay_identity ! jpegdec ! video/x-raw,format=RGB ! identity name=jpegdec_identity ! queue ! identity name=queue_identity ! appsink emit-signals=true name=appsink sync=false";
    const std::string h264Pipeline_ = "udpsrc name=udpsrc ! application/x-rtp, media=video, clock-rate=90000, payload=96 ! identity name=udpsrc_identity ! rtph264depay ! identity name=rtpjpegdepay_identity ! avdec_h264 ! videoconvert ! video/x-raw,format=RGB ! identity name=jpegdec_identity ! queue ! identity name=queue_identity ! appsink emit-signals=true name=appsink sync=false";

};
