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
        double diff = std::get<1>(gstreamerFrameLeft_).second - std::get<1>(gstreamerFrameLeft_).first;
        LOG_INFO("%f FPS", 1000.0f / diff);
        return std::get<0>(gstreamerFrameLeft_);
    }

    GstreamerFrame getFrameRight() const {
        double diff = std::get<1>(gstreamerFrameRight_).second - std::get<1>(gstreamerFrameRight_).first;
        LOG_INFO("%f FPS", 1000.0f / diff);
        return std::get<0>(gstreamerFrameRight_);
    }

private:

    static GstFlowReturn newFrameCallback(GstElement *sink, std::tuple<GstreamerFrame, std::pair<double, double>> *frame);

    static void stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    GstElement *pipelineLeft_{};
    GstElement *pipelineRight_{};
    GMainContext *context_{};
    GMainLoop *mainLoop_{};

    std::tuple<GstreamerFrame, std::pair<double, double>> gstreamerFrameLeft_;
    std::tuple<GstreamerFrame, std::pair<double, double>> gstreamerFrameRight_;
};
