#include "pch.h"
#include <gst/gst.h>
#include <gio/gio.h>
#include "log.h"
#include "BS_thread_pool.hpp"

#pragma once


class GstreamerPlayer {
public:

    struct CameraStats {
        double prevTimestamp, currTimestamp;
        double fps;
        uint64_t nvvidconv, jpegenc, rtpjpegpay, udpstream, rtpjpegdepay, jpegdec, queue;
        uint64_t rtpjpegpayTimestamp, udpsrcTimestamp, rtpjpegdepayTimestamp, jpegdecTimestamp, queueTimestamp;
        uint64_t totalLatency;
        uint64_t frameId;
    };

    struct CameraFrame {
        CameraStats *stats;
        unsigned long memorySize = 1920 * 1080 * 3; // Size of single Full HD RGB frame
        void *dataHandle;
    };

    using CamPair = std::pair<CameraFrame, CameraFrame>;

    explicit GstreamerPlayer(BS::thread_pool &threadPool);

    ~GstreamerPlayer() = default;

    void play();

    [[nodiscard]] CameraFrame getFrameLeft() const {
        LOG_INFO("FPS LEFT: %f, latency: %lu", camPair_->first.stats->fps,
                 (unsigned long) camPair_->first.stats->totalLatency);
        return camPair_->first;
    }

    [[nodiscard]] CameraFrame getFrameRight() const {
        LOG_INFO("FPS RIGHT: %f, latency: %lu", camPair_->second.stats->fps,
                 (unsigned long) camPair_->second.stats->totalLatency);
        return camPair_->second;
    }

private:

    static GstFlowReturn newFrameCallback(GstElement *sink, CamPair *pair);

    static void onRtpHeaderMetadata(GstElement *identity, GstBuffer *buffer, gpointer data);

    static void onIdentityHandoff(GstElement *identity, GstBuffer *buffer, gpointer data);

    static void stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static uint64_t getCurrentUs();

    GstElement *pipelineLeft_, *pipelineRight_;
    GMainContext *context_{};
    GMainLoop *mainLoop_{};

    CamPair *camPair_;
};
