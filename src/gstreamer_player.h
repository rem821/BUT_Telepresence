#include <gst/gst.h>
#include <gio/gio.h>
#include "BS_thread_pool.hpp"

#pragma once


class GstreamerPlayer {
public:
    struct GstreamerFrame {
        unsigned long memorySize;
        void* dataHandle;
    };

    GstreamerPlayer(BS::thread_pool &threadPool);

    ~GstreamerPlayer() = default;

    void play();

private:

    static GstFlowReturn newFrameCallback(GstElement *sink, GstreamerFrame *frame);
    static void stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);
    static void errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline);

    GstElement* pipeline_{};
    GMainContext* context_{};
    GMainLoop* mainLoop_{};
    GstElement* videoSink_{};

    pthread_t gstThread_{};

    bool isInitialized_ = false;

    GstreamerFrame *gstreamerFrame_{};
};
