#include "pch.h"
#include <gst/gst.h>
#include <gio/gio.h>
#include "log.h"
#include "common.h"
#include "BS_thread_pool.hpp"
#include "ntp_timer.h"

#pragma once


class GstreamerPlayer {
public:

    explicit GstreamerPlayer(CamPair *camPair, NtpTimer *ntpTimer);

    ~GstreamerPlayer() = default;

    void configurePipeline(BS::thread_pool<BS::tp::none> &threadPool, const StreamingConfig &config);

private:

    using GStreamerCallbackObj = std::pair<CamPair*, NtpTimer*>;

    static GstFlowReturn newFrameCallback(GstElement *sink, GStreamerCallbackObj *callbackObj);

    static void onRtpHeaderMetadata(GstElement *identity, GstBuffer *buffer, gpointer data);

    static void onIdentityHandoff(GstElement *identity, GstBuffer *buffer, gpointer data);

    static void stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    void listAvailableDecoders();

    void dumpGstreamerFeatures();

    gboolean printGstreamerFeature(const GstPluginFeature *feature, gpointer user_data);

    GstElement *pipelineLeft_{}, *pipelineRight_{};
    GMainContext *context_{};
    GMainLoop *mainLoop_{};

    CamPair *camPair_;
    GStreamerCallbackObj *callbackObj_;

    NtpTimer *ntpTimer_;

    const std::string jpegPipeline_ =   "udpsrc name=udpsrc ! capsfilter name=rtp_capsfilter caps=\"application/x-rtp, encoding-name=JPEG, payload=26\" ! identity name=udpsrc_ident ! rtpjpegdepay ! identity name=rtpdepay_ident ! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=RGB ! identity name=dec_ident ! identity name=queue_ident ! appsink emit-signals=true name=appsink sync=false";
    //const std::string jpegPipeline_ = "udpsrc name=udpsrc ! capsfilter name=rtp_capsfilter caps=\"application/x-rtp, encoding-name=JPEG, payload=26\" ! identity name=udpsrc_ident ! rtpjpegdepay ! identity name=rtpdepay_ident ! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=RGB ! aspectratiocrop aspect-ratio=43/46 ! videoscale ! video/x-raw,width=2208,height=2064 ! identity name=dec_ident ! identity name=queue_ident ! appsink emit-signals=true name=appsink sync=false";
    const std::string h264Pipeline_ =   "udpsrc name=udpsrc buffer-size=100000 ! capsfilter name=rtp_capsfilter caps=\"application/x-rtp, encoding-name=H264, media=video, clock-rate=90000, payload=96\" ! identity name=udpsrc_ident ! rtph264depay ! identity name=rtpdepay_ident ! decodebin3 ! videoconvert ! video/x-raw,format=RGB ! identity name=dec_ident ! queue ! identity name=queue_ident ! appsink emit-signals=true name=appsink sync=false";

};
