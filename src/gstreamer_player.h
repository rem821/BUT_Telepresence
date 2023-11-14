#include "pch.h"
#include <gst/gst.h>
#include <gio/gio.h>
#include "log.h"
#include "BS_thread_pool.hpp"

#pragma once


class GstreamerPlayer {
public:
    struct GstreamerFrame {
        unsigned long memorySize;
        void *dataHandle;
    };

    GstreamerPlayer(BS::thread_pool &threadPool);

    ~GstreamerPlayer() = default;

    void play();

    GstreamerFrame getFrameLeft() const {
        double diff = std::get<2>(gstreamerFrames_).second - std::get<2>(gstreamerFrames_).first;
        LOG_INFO("%f FPS", 1000.0f / diff);
        return std::get<0>(gstreamerFrames_);
    }

    GstreamerFrame getFrameRight() const { return std::get<1>(gstreamerFrames_); }

private:

    static GstFlowReturn newFrameCallback(GstElement *sink, std::tuple<GstreamerFrame, GstreamerFrame, std::pair<double, double>> *frames);

    static void stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    GstElement *pipeline_;
    GMainContext *context_{};
    GMainLoop *mainLoop_{};

    std::tuple<GstreamerFrame, GstreamerFrame, std::pair<double, double>> gstreamerFrames_;
};
