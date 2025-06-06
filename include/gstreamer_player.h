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

    void configurePipeline(BS::thread_pool<BS::tp::none> &threadPool, const StreamingConfig &config, const bool combinedStreaming);

private:

    using GStreamerCallbackObj = std::pair<CamPair*, NtpTimer*>;

    static GstFlowReturn newFrameCallback(GstElement *sink, GStreamerCallbackObj *callbackObj);

    static void onRtpHeaderMetadata(GstElement *identity, GstBuffer *buffer, gpointer data);

    static void onIdentityHandoff(GstElement *identity, GstBuffer *buffer, gpointer data);

    static void stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static GstPadProbeReturn udpPacketProbeCallback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);

    static void gstCustomLog(GstDebugCategory *category, GstDebugLevel level, const gchar *file, const gchar *function, gint line,
                                       GObject *object, GstDebugMessage *message, gpointer user_data);

    void listAvailableDecoders();

    void dumpGstreamerFeatures();

    gboolean printGstreamerFeature(const GstPluginFeature *feature, gpointer user_data);

    GstElement *pipelineLeft_{}, *pipelineRight_{}, *pipelineCombined_{};
    GMainContext *context_{};
    GMainLoop *mainLoop_{};

    CamPair *camPair_;
    GStreamerCallbackObj *callbackObj_;

    NtpTimer *ntpTimer_;

    const std::string jpegPipeline_ =   "udpsrc name=udpsrc ! capsfilter name=rtp_capsfilter caps=\"application/x-rtp, encoding-name=JPEG, payload=26\" ! identity name=udpsrc_ident ! rtpjpegdepay ! identity name=rtpdepay_ident ! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=RGB ! identity name=dec_ident ! identity name=queue_ident ! appsink emit-signals=true name=appsink sync=false";
    //const std::string jpegPipeline_ = "udpsrc name=udpsrc ! capsfilter name=rtp_capsfilter caps=\"application/x-rtp, encoding-name=JPEG, payload=26\" ! identity name=udpsrc_ident ! rtpjpegdepay ! identity name=rtpdepay_ident ! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=RGB ! aspectratiocrop aspect-ratio=43/46 ! videoscale ! video/x-raw,width=2208,height=2064 ! identity name=dec_ident ! identity name=queue_ident ! appsink emit-signals=true name=appsink sync=false";
    const std::string h264Pipeline_ =   "udpsrc name=udpsrc buffer-size=0 ! capsfilter name=rtp_capsfilter caps=\"application/x-rtp, encoding-name=H264, media=video, clock-rate=90000, payload=96\" ! identity name=udpsrc_ident ! rtpjitterbuffer latency=0 do-lost=true drop-on-latency=true do-retransmission=false ! rtph264depay ! identity name=rtpdepay_ident ! h264parse ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! decodebin3 ! videoconvert ! video/x-raw,format=RGB ! identity name=dec_ident ! identity name=queue_ident ! appsink emit-signals=true name=appsink sync=false";
    const std::string h265Pipeline_ =   "udpsrc name=udpsrc buffer-size=10000000 ! capsfilter name=rtp_capsfilter caps=\"application/x-rtp, encoding-name=H265, media=video, clock-rate=90000, payload=96\" ! identity name=udpsrc_ident ! rtpjitterbuffer latency=0 do-lost=true drop-on-latency=true do-retransmission=false ! rtph265depay ! identity name=rtpdepay_ident ! h265parse ! video/x-h265, stream-format=byte-stream, alignment=au ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! decodebin3 ! videoconvert ! video/x-raw,format=RGB ! identity name=dec_ident ! identity name=queue_ident ! appsink emit-signals=true name=appsink sync=false";
    const std::string jpegPipelineCombined_ = "udpsrc name=udpsrc ! capsfilter name=rtp_capsfilter caps=\"application/x-rtp, encoding-name=JPEG, payload=26\" ! identity name=udpsrc_ident ! rtpjpegdepay ! identity name=rtpdepay_ident ! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=RGB ! identity name=dec_ident ! identity name=queue_ident ! appsink emit-signals=true name=appsink sync=false";
};
