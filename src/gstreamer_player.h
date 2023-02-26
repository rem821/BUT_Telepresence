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

    GstreamerFrame getFrame() const { return gstreamerFrame_; }

private:

    static GstFlowReturn newFrameCallback(GstElement *sink, GstreamerFrame *frame);

    static void stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    static void errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    void dumpGstreamerFeatures();

    static gboolean printGstreamerFeature(const GstPluginFeature *feature, gpointer user_data);

    static void *YUV420toRGB(void *image);

    GstElement *pipeline_{};
    GMainContext *context_{};
    GMainLoop *mainLoop_{};

    bool isInitialized_ = false;

    GstreamerFrame gstreamerFrame_{};
};
