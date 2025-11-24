#include "gstreamer_player.h"
#include "util_egl.h"
#include <ctime>
#include <gst/rtp/rtp.h>
#include <fmt/format.h>
#include <gst/video/video.h>
#include <GLES3/gl3.h>
#include <gst/gl/gstglmemory.h>
#include <GLES2/gl2ext.h>

#define SINK_CAPS \
    "video/x-raw(memory:GLMemory), "                         \
    "format = (string) RGBA, "                               \
    "width = (int) [ 1, max ], "        \
    "height = (int) [ 1, max ], "      \
    "framerate = (fraction) [ 0/1, max ], " \
    "texture-target = (string) { 2D, external-oes } "

GstreamerPlayer::GstreamerPlayer(CamPair *camPair, NtpTimer *ntpTimer) : camPair_(camPair), ntpTimer_(ntpTimer) {
    guint major, minor, micro, nano;
    gst_version(&major, &minor, &micro, &nano);
    LOG_INFO("Running GStreamer version: %d.%d.%d.%d", major, minor, micro, nano);

    //listAvailableDecoders();
    //listGstreamerPlugins();

//    GstElementFactory *factory = gst_element_factory_find("amcviddec-omxqcomvideodecoderavc");
//    if (factory) {
//        const GList *pads = gst_element_factory_get_static_pad_templates(factory);
//        for (const GList *pad = pads; pad != nullptr; pad = pad->next) {
//            GstPadTemplate *template_ = static_cast<GstPadTemplate *>(pad->data);
//
//            const gchar *pad_name = template_->name_template;
//            const gchar *direction = (template_->direction == GST_PAD_SRC) ? "SRC" : "SINK";
//
//            LOG_INFO("Gstreamer: Pad: %s | Direction: %s", pad_name, direction);
//        }
//
//        gst_object_unref(factory);
//    } else {
//        LOG_ERROR("Gstreamer: Element factory not found for amcviddec-omxqcomvideodecoderavc");
//    }

//    GList *list = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_DECODER, GST_RANK_NONE);
//    for (GList *l = list; l; l = l->next) {
//        auto *f = GST_ELEMENT_FACTORY(l->data);
//        const gchar *name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f));
//        const gchar *longname = gst_element_factory_get_longname(f);
//        const gchar *klass = gst_element_factory_get_klass(f);
//        if (g_strrstr(name, "amc")) {
//            LOG_INFO("Gstreamer: AMC DEC: %s  | %s  | %s", name, longname, klass);
//        }
//    }
//    gst_plugin_feature_list_free(list);

    EGLDisplay egl_dpy = egl_get_display();
    EGLContext egl_ctx = egl_get_context();
    GstGLDisplay *gst_display = reinterpret_cast<GstGLDisplay *>(gst_gl_display_egl_new_with_egl_display(egl_dpy));
    glContext_ = gst_gl_context_new_wrapped(gst_display, (guintptr) egl_ctx, GST_GL_PLATFORM_EGL, GST_GL_API_GLES2);
    gContext_ = gst_context_new("gst.gl.app_context", TRUE);
    GstStructure *s = gst_context_writable_structure(gContext_);
    gst_structure_set(s, "display", GST_TYPE_GL_DISPLAY, gst_display, "context", GST_TYPE_GL_CONTEXT, glContext_, nullptr);
    gst_object_unref(gst_display);


    /* Create our own GLib Main Context and make it the default one */
    gMainContext_ = g_main_context_new();
    g_main_context_push_thread_default(gMainContext_);
}

void
GstreamerPlayer::configurePipeline(BS::thread_pool<BS::tp::none> &threadPool, const StreamingConfig &config, const bool combinedStreaming) {
    GstBus *bus;
    GSource *bus_source;
    GError *error = nullptr;

    LOG_INFO("(Re)configuring GStreamer pipelines");

    // Stop and clean up existing pipelines if they exist
    if (pipelineLeft_) {
        LOG_INFO("Stopping the left pipeline before reconfiguration");
        gst_element_send_event(pipelineLeft_, gst_event_new_eos());
        gst_element_set_state(pipelineLeft_, GST_STATE_NULL);
        gst_object_unref(pipelineLeft_);
        pipelineLeft_ = nullptr;
    }
    if (pipelineRight_) {
        LOG_INFO("Stopping the right pipeline before reconfiguration");
        gst_element_send_event(pipelineRight_, gst_event_new_eos());
        gst_element_set_state(pipelineRight_, GST_STATE_NULL);
        gst_object_unref(pipelineRight_);
        pipelineRight_ = nullptr;
    }
    // Stop the main loop if running

    if (mainLoop_) {
        LOG_INFO("Stopping GStreamer main loop before reconfiguration");
        g_main_loop_quit(mainLoop_);  // Signal the loop to stop
    }

    //Init the CameraFrame data structure
    callbackObj_ = new GStreamerCallbackObj(camPair_, ntpTimer_);
    camPair_->first.stats = new CameraStats();
    camPair_->second.stats = new CameraStats();

    auto *emptyFrameLeft = new unsigned char[camPair_->first.memorySize];
    memset(emptyFrameLeft, 0, sizeof(emptyFrameLeft));
    camPair_->first.dataHandle = (void *) emptyFrameLeft;

    auto *emptyFrameRight = new unsigned char[camPair_->second.memorySize];
    memset(emptyFrameRight, 0, sizeof(emptyFrameRight));
    camPair_->second.dataHandle = (void *) emptyFrameRight;

    camPair_->first.frameWidth = config.resolution.getWidth();
    camPair_->first.frameHeight = config.resolution.getHeight();
    camPair_->second.frameWidth = config.resolution.getWidth();
    camPair_->second.frameHeight = config.resolution.getHeight();
    camPair_->first.memorySize = camPair_->first.frameWidth * camPair_->first.frameHeight * 3;
    camPair_->second.memorySize = camPair_->second.frameWidth * camPair_->second.frameHeight * 3;


    // Create new pipelines based on the provided configuration
    switch (config.codec) {
        case Codec::JPEG:
            pipelineLeft_ = gst_parse_launch(jpegPipeline_.c_str(), &error);
            pipelineRight_ = gst_parse_launch(jpegPipeline_.c_str(), &error);
            break;
        case Codec::VP8:
            //TODO:
            break;
        case Codec::VP9:
            //TODO:
            break;
        case Codec::H264:
            pipelineLeft_ = gst_parse_launch(h264Pipeline_.c_str(), &error);
            pipelineRight_ = gst_parse_launch(h264Pipeline_.c_str(), &error);
            break;
        case Codec::H265:
            pipelineLeft_ = gst_parse_launch(h265Pipeline_.c_str(), &error);
            pipelineRight_ = gst_parse_launch(h265Pipeline_.c_str(), &error);
            break;
        default:
            break;
    }

    if (error) {
        LOG_ERROR("Unable to build pipeline!: %s", error->message);
        throw std::runtime_error("Unable to build pipeline!");
    }

    if (combinedStreaming) {
        std::string xDimString = fmt::format("{},{}", config.resolution.getWidth(), config.resolution.getHeight() * 2);

        pipelineCombined_ = gst_parse_launch(jpegPipelineCombined_.c_str(), &error);

        GstElement *udpsrc_ident = gst_bin_get_by_name(GST_BIN(pipelineCombined_), "udpsrc_ident");
        GstElement *rtpdepay_ident = gst_bin_get_by_name(GST_BIN(pipelineCombined_), "rtpdepay_ident");
        GstElement *dec_ident = gst_bin_get_by_name(GST_BIN(pipelineCombined_), "dec_ident");
        GstElement *queue_ident = gst_bin_get_by_name(GST_BIN(pipelineCombined_), "queue_ident");

        GstElement *udpsrc = gst_bin_get_by_name(GST_BIN(pipelineCombined_), "udpsrc");
        GstPad *pad = gst_element_get_static_pad(udpsrc, "src");
        if (pad) {
            gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, udpPacketProbeCallback, nullptr, nullptr);
            gst_object_unref(pad);
        }
        g_object_set(udpsrc, "port", IP_CONFIG_COMBINED_CAMERA_PORT, NULL);

        int payload = 26;
        if (config.codec != Codec::JPEG) {
            payload = 96;
        }

        GstElement *rtp_capsfilter = gst_bin_get_by_name(GST_BIN(pipelineCombined_), "rtp_capsfilter");
        GstCaps *new_caps = gst_caps_new_simple("application/x-rtp",
                                                "encoding-name", G_TYPE_STRING, CodecToString(config.codec).c_str(),
                                                "payload", G_TYPE_INT, payload,
                                                "x-dimensions", G_TYPE_STRING, xDimString.c_str(),
                                                NULL);
        g_object_set(rtp_capsfilter, "caps", new_caps, NULL);
        gst_caps_unref(new_caps);
        gst_object_unref(rtp_capsfilter);

        GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipelineCombined_), "appsink");
        gst_element_set_context(GST_ELEMENT (appsink), gContext_);
        gst_element_set_name(pipelineCombined_, "pipeline_combined");
        gst_element_set_state(pipelineCombined_, GST_STATE_READY);

        bus = gst_element_get_bus(pipelineCombined_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr, nullptr);
        g_source_attach(bus_source, gMainContext_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipelineCombined_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback, pipelineCombined_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback, pipelineCombined_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback, pipelineCombined_);
        g_signal_connect(G_OBJECT(appsink), "new-sample", (GCallback) newFrameCallback, callbackObj_);
        g_signal_connect(G_OBJECT(udpsrc_ident), "handoff", (GCallback) onRtpHeaderMetadata, callbackObj_);
        g_signal_connect(G_OBJECT(rtpdepay_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(dec_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(queue_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        gst_object_unref(udpsrc_ident);
        gst_object_unref(rtpdepay_ident);
        gst_object_unref(dec_ident);
        gst_object_unref(queue_ident);
        gst_object_unref(appsink);
        gst_object_unref(bus);

        gst_element_set_state(pipelineCombined_, GST_STATE_PLAYING);
    } else {
        std::string xDimString = fmt::format("{},{}", config.resolution.getWidth(), config.resolution.getHeight());

        GstElement *leftudpsrc_ident = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "udpsrc_ident");
        GstElement *leftrtpdepay_ident = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "rtpdepay_ident");
        GstElement *leftdec_ident = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "dec_ident");
        GstElement *leftqueue_ident = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "queue_ident");

        GstElement *leftudpsrc = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "udpsrc");
        GstPad *pad = gst_element_get_static_pad(leftudpsrc, "src");
        if (pad) {
            gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, udpPacketProbeCallback, nullptr, nullptr);
            gst_object_unref(pad);
        }
        g_object_set(leftudpsrc, "port", IP_CONFIG_LEFT_CAMERA_PORT, NULL);

        int payload = 26;
        if (config.codec != Codec::JPEG) {
            payload = 96;
        }

        GstElement *rtp_capsfilter_left = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "rtp_capsfilter");
        GstCaps *new_caps_left = gst_caps_new_simple("application/x-rtp",
                                                     "encoding-name", G_TYPE_STRING, CodecToString(config.codec).c_str(),
                                                     "payload", G_TYPE_INT, payload,
                                                     "x-dimensions", G_TYPE_STRING, xDimString.c_str(),
                                                     NULL);
        g_object_set(rtp_capsfilter_left, "caps", new_caps_left, NULL);
        gst_caps_unref(new_caps_left);
        gst_object_unref(rtp_capsfilter_left);

        GstElement *leftglsink = nullptr;
        GstElement *leftappsink = nullptr;
        if (config.codec != Codec::JPEG) {
            leftglsink = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "glsink");
            gst_element_set_context(leftglsink, gContext_);

            g_autoptr(GstCaps) caps = gst_caps_from_string(SINK_CAPS);
            leftappsink = gst_element_factory_make("appsink", nullptr);
            gst_element_set_context(leftappsink, gContext_);
            g_object_set(leftappsink, "caps", caps, "max-buffers", 1, "drop", true, "emit-signals", true, "sync", true, NULL);

            g_autoptr(GstElement) glsinkbin = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "glsink");
            g_object_set(glsinkbin, "sink", leftappsink, NULL);
        } else {
            leftappsink = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "appsink");
            gst_element_set_context(GST_ELEMENT (leftappsink), gContext_);
        }


        bus = gst_element_get_bus(pipelineLeft_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr, nullptr);
        g_source_attach(bus_source, gMainContext_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback, pipelineLeft_);
        if (config.codec != Codec::JPEG) {
            //g_signal_connect(G_OBJECT(leftglsink), "client-draw", (GCallback) GstreamerPlayer::onClientDraw, callbackObj_);
            g_signal_connect(G_OBJECT(leftappsink), "new-sample", (GCallback) newFrameCallback, callbackObj_);
        } else {
            g_signal_connect(G_OBJECT(leftappsink), "new-sample", (GCallback) newFrameCallback, callbackObj_);
        }
        g_signal_connect(G_OBJECT(leftudpsrc_ident), "handoff", (GCallback) onRtpHeaderMetadata, callbackObj_);
        g_signal_connect(G_OBJECT(leftrtpdepay_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(leftdec_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(leftqueue_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        gst_object_unref(leftudpsrc_ident);
        gst_object_unref(leftrtpdepay_ident);
        gst_object_unref(leftdec_ident);
        gst_object_unref(leftqueue_ident);
        if (config.codec != Codec::JPEG) {
            gst_object_unref(leftglsink);
        }
        gst_object_unref(leftappsink);
        gst_object_unref(bus);

        gst_element_set_name(pipelineLeft_, "pipeline_left");
        gst_element_set_state(pipelineLeft_, GST_STATE_READY);

        /* --------------------------------------------------------------------------------------------------------------------------------------------------------- */

        GstElement *rightudpsrc_ident = gst_bin_get_by_name(GST_BIN(pipelineRight_), "udpsrc_ident");
        GstElement *rightrtpdepay_ident = gst_bin_get_by_name(GST_BIN(pipelineRight_), "rtpdepay_ident");
        GstElement *rightdec_ident = gst_bin_get_by_name(GST_BIN(pipelineRight_), "dec_ident");
        GstElement *rightqueue_ident = gst_bin_get_by_name(GST_BIN(pipelineRight_), "queue_ident");

        GstElement *rightudpsrc = gst_bin_get_by_name(GST_BIN(pipelineRight_), "udpsrc");
        g_object_set(rightudpsrc, "port", IP_CONFIG_RIGHT_CAMERA_PORT, NULL);
        pad = gst_element_get_static_pad(rightudpsrc, "src");
        if (pad) {
            gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, udpPacketProbeCallback, nullptr, nullptr);
            gst_object_unref(pad);
        }

        GstElement *rtp_capsfilter_right = gst_bin_get_by_name(GST_BIN(pipelineRight_), "rtp_capsfilter");
        GstCaps *new_caps_right = gst_caps_new_simple("application/x-rtp",
                                                      "encoding-name", G_TYPE_STRING, CodecToString(config.codec).c_str(),
                                                      "payload", G_TYPE_INT, payload,
                                                      "x-dimensions", G_TYPE_STRING, xDimString.c_str(),
                                                      NULL);
        g_object_set(rtp_capsfilter_right, "caps", new_caps_right, NULL);
        gst_caps_unref(new_caps_right);
        gst_object_unref(rtp_capsfilter_right);

        GstElement *rightglsink = nullptr;
        GstElement *rightappsink = nullptr;
        if (config.codec != Codec::JPEG) {
            rightglsink = gst_bin_get_by_name(GST_BIN(pipelineRight_), "glsink");
            gst_element_set_context(rightglsink, gContext_);

            g_autoptr(GstCaps) caps = gst_caps_from_string(SINK_CAPS);
            rightappsink = gst_element_factory_make("appsink", nullptr);
            gst_element_set_context(rightappsink, gContext_);
            g_object_set(rightappsink, "caps", caps, "max-buffers", 1, "drop", true, "emit-signals", true, "sync", true, NULL);

            g_autoptr(GstElement) glsinkbin = gst_bin_get_by_name(GST_BIN(pipelineRight_), "glsink");
            g_object_set(glsinkbin, "sink", rightappsink, NULL);
        } else {
            rightappsink = gst_bin_get_by_name(GST_BIN(pipelineRight_), "appsink");
            gst_element_set_context(GST_ELEMENT (rightappsink), gContext_);
        }

        bus = gst_element_get_bus(pipelineRight_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr, nullptr);
        g_source_attach(bus_source, gMainContext_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback, pipelineRight_);
        if (config.codec != Codec::JPEG) {
            g_signal_connect(G_OBJECT(rightappsink), "new-sample", (GCallback) newFrameCallback, callbackObj_);
        } else {
            g_signal_connect(G_OBJECT(rightappsink), "new-sample", (GCallback) newFrameCallback, callbackObj_);
        }
        g_signal_connect(G_OBJECT(rightudpsrc_ident), "handoff", (GCallback) onRtpHeaderMetadata, callbackObj_);
        g_signal_connect(G_OBJECT(rightrtpdepay_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(rightdec_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(rightqueue_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        gst_object_unref(rightudpsrc_ident);
        gst_object_unref(rightrtpdepay_ident);
        gst_object_unref(rightdec_ident);
        gst_object_unref(rightqueue_ident);
        if (config.codec != Codec::JPEG) {
            gst_object_unref(rightglsink);
        }
        gst_object_unref(rightappsink);
        gst_object_unref(bus);

        gst_element_set_name(pipelineRight_, "pipeline_right");
        gst_element_set_state(pipelineRight_, GST_STATE_READY);

        gst_element_set_state(pipelineLeft_, GST_STATE_PLAYING);
        gst_element_set_state(pipelineRight_, GST_STATE_PLAYING);
    }

    threadPool.detach_task([&]() {
        /* Create a GLib Main Loop and set it to run */
        LOG_INFO("GSTREAMER entering the main loop");
        mainLoop_ = g_main_loop_new(gMainContext_, FALSE);

        g_main_loop_run(mainLoop_);
        LOG_INFO("GSTREAMER exited the main loop");
        g_main_loop_unref(mainLoop_);
        mainLoop_ = nullptr;
    });
}

GstFlowReturn GstreamerPlayer::newFrameCallback(GstElement *sink, GStreamerCallbackObj *callbackObj)
{
    GstSample *sample = nullptr;

    /* Retrieve the buffer from appsink */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (!sample) {
        return GST_FLOW_ERROR;
    }

    //LOG_INFO("GSTREAMER - sample arrived");

    CamPair *pair = callbackObj->first;
    //pair->first.hasGlTexture  = false;
    //pair->second.hasGlTexture = false;

    GstObject *parent = GST_OBJECT(sink);
    while (GST_OBJECT_PARENT(parent) != nullptr) {
        parent = GST_OBJECT_PARENT(parent);
    }
    std::string pipelineName = GST_OBJECT_NAME(parent);

    const bool isCombined   = (pipelineName == "pipeline_combined");
    const bool isLeftCamera = (pipelineName == "pipeline_left");
    const bool isRightCamera = (pipelineName == "pipeline_right");

    if (isCombined) {
        //LOG_INFO("GSTREAMER New GStreamer combined frame received");
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstMapInfo mapInfo{};

        if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ)) {
            LOG_ERROR("GSTREAMER: Failed to map buffer in combined callback");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }

        // Split combined frame into left + right
        memcpy(pair->first.dataHandle,  mapInfo.data,                     pair->first.memorySize);
        memcpy(pair->second.dataHandle, mapInfo.data + pair->first.memorySize - 3, pair->second.memorySize);

        gst_buffer_unmap(buffer, &mapInfo);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    CameraFrame &frame = isLeftCamera ? pair->first : pair->second;

    if (isLeftCamera) {
        //LOG_INFO("GSTREAMER NEW_SAMPLE - New GStreamer frame received from left camera");
    } else if (isRightCamera) {
        //LOG_INFO("GSTREAMER NEW_SAMPLE - New GStreamer frame received from right camera");
    } else {
        //LOG_INFO("GSTREAMER NEW_SAMPLE - New GStreamer frame received from unknown pipeline: %s", pipelineName.c_str());
    }

    // Update FPS stats
    frame.stats->prevTimestamp = frame.stats->currTimestamp;
    frame.stats->currTimestamp = callbackObj->second->GetCurrentTimeUs();
    if (frame.stats->prevTimestamp != 0) {
        double diff = frame.stats->currTimestamp - frame.stats->prevTimestamp;
        frame.stats->fps = 1e6f / diff;
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps   *caps   = gst_sample_get_caps(sample);

    if (!caps) {
        LOG_ERROR("GSTREAMER: Sample has no caps");
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    GstStructure *st = gst_caps_get_structure(caps, 0);
    const gchar *tex_target_str = gst_structure_get_string(st, "texture-target");
    if (g_strcmp0(tex_target_str, "external-oes") == 0) {
        frame.glTarget = GL_TEXTURE_EXTERNAL_OES;
    } else {
        frame.glTarget = GL_TEXTURE_2D;  // fallback / SW GL path
    }

    // Check whether this is GLMemory (HW decode) or plain system memory (JPEG)
    GstCapsFeatures *features = gst_caps_get_features(caps, 0);
    const bool isGLMemory = (features != nullptr) && gst_caps_features_contains(features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);

    if (!isGLMemory) {
        // -----------------------------------------------------------------
        // SOFTWARE PATH (e.g. JPEG) – CPU buffer, memcpy to dataHandle
        // -----------------------------------------------------------------
        GstMapInfo mapInfo{};
        if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ)) {
            LOG_ERROR("GSTREAMER: Failed to map CPU buffer");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }

        // Make sure memorySize is correct (width * height * 3 for RGB, etc.)
        memcpy(frame.dataHandle, mapInfo.data, frame.memorySize);

        gst_buffer_unmap(buffer, &mapInfo);
        gst_sample_unref(sample);

        frame.hasGlTexture = false;  // we uploaded into CPU buffer
        return GST_FLOW_OK;

    } else {
        // -----------------------------------------------------------------
        // HARDWARE PATH (H.264 → GLMemory, possibly external-oes)
        // NO CPU mapping – grab texture ID via GstVideoFrame + GST_MAP_GL
        // -----------------------------------------------------------------
        GstVideoInfo vinfo;
        if (!gst_video_info_from_caps(&vinfo, caps)) {
            LOG_ERROR("GSTREAMER: Failed to get video info from caps");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }

        GstVideoFrame vframe;
        if (!gst_video_frame_map(&vframe, &vinfo, buffer,
                                 (GstMapFlags)(GST_MAP_READ | GST_MAP_GL))) {
            LOG_ERROR("GSTREAMER: Failed to map video frame as GL (External OES?)");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }

        // vframe.data[0] contains a GLuint* with the texture ID
        GLuint tex_id = *(guint *)vframe.data[0];
        //LOG_INFO("GSTREAMER GL frame: texture id = %u", tex_id);

        frame.glTexture   = tex_id;
        frame.hasGlTexture = true;
        frame.frameWidth  = GST_VIDEO_INFO_WIDTH(&vinfo);
        frame.frameHeight = GST_VIDEO_INFO_HEIGHT(&vinfo);

        gst_video_frame_unmap(&vframe);
        gst_sample_unref(sample);

        // NOTE: we do NOT touch frame.dataHandle here – your render path
        // should use frame.hasGlTexture + frame.glTexture and bind as
        // GL_TEXTURE_EXTERNAL_OES / GL_TEXTURE_2D accordingly.
        return GST_FLOW_OK;
    }
}

void GstreamerPlayer::onRtpHeaderMetadata(GstElement *identity, GstBuffer *buffer, gpointer data) {
    auto *obj = reinterpret_cast<GStreamerCallbackObj *>(data);
    auto *pair = obj->first;
    auto *ntpTimer = obj->second;

    bool isLeftCamera = std::string(identity->object.parent->name) == "pipeline_left";
    auto stats = isLeftCamera ? pair->first.stats : pair->second.stats;
    stats->totalLatency = 0;

    GstRTPBuffer rtp_buf = GST_RTP_BUFFER_INIT;
    gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp_buf);
    gpointer myInfoBuf = nullptr;
    guint size_64 = 8;
    guint8 appbits = 1;
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 0, &myInfoBuf, &size_64) != 0) {
        stats->frameId = *(static_cast<uint64_t *>(myInfoBuf));
        //LOG_INFO("GSTREAMER: New frameid from %s - number of packets: %s", identity->object.parent->name, std::to_string(stats->_packetsPerFrame).c_str());
        stats->_packetsPerFrame = 0;
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 1, &myInfoBuf, &size_64) != 0) {
        stats->vidConv = *(static_cast<uint64_t *>(myInfoBuf));
        //LOG_INFO("GSTREAMER: New vidconv from %s", identity->object.parent->name);
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 2, &myInfoBuf, &size_64) != 0) {
        stats->enc = *(static_cast<uint64_t *>(myInfoBuf));
        //LOG_INFO("GSTREAMER: New enc from %s", identity->object.parent->name);
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 3, &myInfoBuf, &size_64) != 0) {
        stats->rtpPay = *(static_cast<uint64_t *>(myInfoBuf));
        //LOG_INFO("GSTREAMER: New rtppay from %s", identity->object.parent->name);
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 4, &myInfoBuf, &size_64) != 0) {
        stats->rtpPayTimestamp = *(static_cast<uint64_t *>(myInfoBuf));
        //LOG_INFO("GSTREAMER: New udpsink timestamp from %s", identity->object.parent->name);
    }
    gst_rtp_buffer_unmap(&rtp_buf);

    //LOG_INFO("GSTREAMER: New rtp header from %s frame: %s", identity->object.parent->name, std::to_string(stats->frameId).c_str());
    // This is so the last packet of rtp gets saved
    stats->udpSrcTimestamp = ntpTimer->GetCurrentTimeUs();
    //stats->udpStream = stats->udpSrcTimestamp - stats->rtpPayTimestamp;
    stats->_packetsPerFrame += 1;
}

void GstreamerPlayer::onIdentityHandoff(GstElement *identity, GstBuffer *buffer, gpointer data) {
    auto *obj = reinterpret_cast<GStreamerCallbackObj *>(data);
    auto *pair = obj->first;
    auto *ntpTimer = obj->second;

    bool isLeftCamera = std::string(identity->object.parent->name) == "pipeline_left";
    auto *stats = isLeftCamera ? pair->first.stats : pair->second.stats;

    if (std::string(identity->object.name) == "rtpdepay_ident") {
        stats->rtpDepayTimestamp = ntpTimer->GetCurrentTimeUs();
        stats->rtpDepay = stats->rtpDepayTimestamp - stats->udpSrcTimestamp;
        stats->udpStream = stats->rtpDepayTimestamp - stats->rtpPayTimestamp;
        //LOG_INFO("GSTREAMER: UDP streaming took: %s ms", std::to_string(stats->udpStream / 1000).c_str());
    } else if (std::string(identity->object.name) == "dec_ident") {
        stats->decTimestamp = ntpTimer->GetCurrentTimeUs();
        stats->dec = stats->decTimestamp - stats->rtpDepayTimestamp;

    } else if (std::string(identity->object.name) == "queue_ident") {
        stats->queueTimestamp = ntpTimer->GetCurrentTimeUs();
        stats->queue = stats->queueTimestamp - stats->decTimestamp;
        stats->totalLatency = stats->vidConv + stats->enc + stats->rtpPay + stats->udpStream + stats->rtpDepay + stats->dec + stats->queue;
//        LOG_INFO(
//                "GSTREAMER: %s Pipeline latencies: vidconv: %lu, enc: %lu, rtpPay: %lu, udpStream: %lu, rtpDepay: %lu, dec: %lu, queue: %lu, total: %lu",
//                identity->object.parent->name,
//                (unsigned long) stats->vidConv, (unsigned long) stats->enc,
//                (unsigned long) stats->rtpPay, (unsigned long) stats->udpStream,
//                (unsigned long) stats->rtpDepay, (unsigned long) stats->dec,
//                (unsigned long) stats->queue,
//                (unsigned long) stats->totalLatency);
    }
}

void GstreamerPlayer::stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

    LOG_INFO("GSTREAMER element %s state changed to: %s", GST_MESSAGE_SRC(msg)->name, gst_element_state_get_name(new_state));
}

void GstreamerPlayer::infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_info(msg, &err, &debug_info);

    LOG_INFO("GSTREAMER info received from element: %s, %s", GST_OBJECT_NAME(msg->src), err->message);
}

void GstreamerPlayer::warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_warning(msg, &err, &debug_info);

    LOG_INFO("GSTREAMER warning received from element: %s, %s", GST_OBJECT_NAME(msg->src), err->message);
}

void GstreamerPlayer::errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_error(msg, &err, &debug_info);

    LOG_ERROR("GSTREAMER error received from element: %s, %s", GST_OBJECT_NAME(msg->src), err->message);
}

// Callback function to log packet arrivals
GstPadProbeReturn GstreamerPlayer::udpPacketProbeCallback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    static auto last_time = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
    last_time = now;

    //LOG_INFO("[UDP Packet] Arrived. Interval: %lld ms", elapsed);

    return GST_PAD_PROBE_OK;
}

void GstreamerPlayer::listAvailableDecoders() {
    // Get the list of decoders
    GList *decoders = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_DECODER, GST_RANK_NONE);

    if (!decoders) {
        LOG_INFO("GSTREAMER No decoders found in the GStreamer registry.");
        return;
    }

    // Iterate through the list of decoders
    for (GList *iter = decoders; iter != nullptr; iter = iter->next) {
        GstElementFactory *factory = (GstElementFactory *) iter->data;

        // Get the factory name (suitable for use in pipelines)
        const gchar *name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));

        // Get a description of the element
        const gchar *longname = gst_element_factory_get_longname(factory);

        // Check if it might be hardware-accelerated (e.g., contains "omx", "amc", or "hardware")
        gboolean is_hardware = g_strstr_len(name, -1, "omx") || g_strstr_len(name, -1, "amc") || g_strstr_len(name, -1, "hardware");

        // Log the decoder details
        if (is_hardware) {
            LOG_INFO("GSTREAMER HW Decoder: %s (%s)", name, longname);
        } else {
            LOG_INFO("GSTREAMER SW Decoder: %s (%s)", name, longname);
        }
    }

    // Free the list
    gst_plugin_feature_list_free(decoders);
}

void GstreamerPlayer::listGstreamerPlugins() {
    GstRegistry *registry = gst_registry_get();
    GList *features = gst_registry_get_feature_list(registry, GST_TYPE_ELEMENT_FACTORY);

    for (GList *iter = features; iter != nullptr; iter = iter->next) {
        GstElementFactory *factory = GST_ELEMENT_FACTORY(iter->data);
        const gchar *name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
        const gchar *longname = gst_element_factory_get_longname(factory);
        const gchar *klass = gst_element_factory_get_klass(factory);

        LOG_INFO("GSTREAMER Element: %s — %s (%s)\n", name, longname, klass);
    }

    gst_plugin_feature_list_free(features);
}
