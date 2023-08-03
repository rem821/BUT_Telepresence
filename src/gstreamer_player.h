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

    GstreamerFrame getFrameLeft() const { return gstreamerFrames_.first; }
    GstreamerFrame getFrameRight() const { return gstreamerFrames_.second; }
private:

    static GstFlowReturn newFrameCallback(GstElement *sink, std::pair<GstreamerFrame, GstreamerFrame> *frames);

    static void stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    GstElement *pipeline_;
    GMainContext *context_{};
    GMainLoop *mainLoop_{};

    std::pair<GstreamerFrame, GstreamerFrame> gstreamerFrames_;
};
