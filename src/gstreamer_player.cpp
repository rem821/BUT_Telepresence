#include "gstreamer_player.h"
#include "util_egl.h"
#include <ctime>
#include <gst/rtp/rtp.h>
#include <fmt/format.h>
#include <gst/gl/gstglmemory.h>
#include <GLES3/gl3.h>


GstPadProbeReturn printCapsProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        GstEvent *event = gst_pad_probe_info_get_event(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            gchar *caps_str = gst_caps_to_string(caps);
            g_print("Caps after h265parse: %s\n", caps_str);
            g_free(caps_str);
        }
    }
    return GST_PAD_PROBE_OK;
}

void printElementCaps(std::string element) {
    GstElementFactory *factory = gst_element_factory_find(element.c_str());
    if (factory) {
        const GList *pads = gst_element_factory_get_static_pad_templates(factory);
        for (const GList *pad = pads; pad != nullptr; pad = pad->next) {
            GstPadTemplate *template_ = static_cast<GstPadTemplate *>(pad->data);

            const gchar *pad_name = template_->name_template;
            const gchar *direction = (template_->direction == GST_PAD_SRC) ? "SRC" : "SINK";

            LOG_INFO("%s - Pad: %s | Direction: %s", element.c_str(), pad_name, direction);
        }

        gst_object_unref(factory);
    } else {
        LOG_ERROR("Element factory not found for %s", element.c_str());
    }
}

GstreamerPlayer::GstreamerPlayer(CamPair *camPair, NtpTimer *ntpTimer) : camPair_(camPair), ntpTimer_(ntpTimer) {
    guint major, minor, micro, nano;
    gst_version(&major, &minor, &micro, &nano);
    LOG_INFO("Running GStreamer version: %d.%d.%d.%d", major, minor, micro, nano);

    //listAvailableDecoders();
    //listGstreamerPlugins();

//    printElementCaps("amcviddec-omxqcomvideodecoderhevc");
//    printElementCaps("amcviddec-omxqcomvideodecoderavc");
//    printElementCaps("amcviddec-omxqcomvideodecodervp8");
//    printElementCaps("amcviddec-omxqcomvideodecodervp9");
//    printElementCaps("amcviddec-c2qtiavcdecoder");
//    printElementCaps("amcviddec-c2qtihevcdecoder");
//    printElementCaps("amcviddec-c2qtivp8decoder");
//    printElementCaps("amcviddec-c2qtivp9decoder");

    /* Create our own GLib Main Context and make it the default one */
    context_ = g_main_context_new();
    g_main_context_push_thread_default(context_);

    GstGLDisplay *gl_display = GST_GL_DISPLAY(gst_gl_display_egl_new_with_egl_display(egl_get_display()));
    glContext_ = gst_gl_context_new(gl_display);
    GError *error = nullptr;
    if (!gst_gl_context_create(glContext_, nullptr, &error)) {
        LOG_ERROR("Failed to create GstGLContext: %s", error ? error->message : "unknown error");
        if (error) g_error_free(error);
        return;
    }}

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
        if(config.codec != Codec::JPEG) {
            payload=96;
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
//        GstCaps *gl_caps = gst_caps_new_simple(
//                "video/x-raw",
//                "format", G_TYPE_STRING, "RGBA",
//                "memory", G_TYPE_STRING, "GLMemory",
//                "texture-target", G_TYPE_STRING, "external-oes",
//                NULL
//        );
//        g_object_set(G_OBJECT(appsink), "caps", gl_caps, NULL);
//        gst_caps_unref(gl_caps);

        gst_element_set_name(pipelineCombined_, "pipeline_combined");
        gst_element_set_state(pipelineCombined_, GST_STATE_READY);

        bus = gst_element_get_bus(pipelineCombined_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr, nullptr);
        g_source_attach(bus_source, context_);
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
        GstContext *gst_context_left = gst_context_new("gst.gl.app_context", TRUE);
        gst_structure_set(gst_context_writable_structure(gst_context_left), "context", GST_TYPE_GL_CONTEXT, glContext_, NULL);
        gst_element_set_context(pipelineLeft_, gst_context_left);
        gst_context_unref(gst_context_left);

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
        if(config.codec != Codec::JPEG) {
            payload=96;
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

        GstElement *leftappsink = gst_bin_get_by_name(GST_BIN(pipelineLeft_), "appsink");
//        if (config.codec != Codec::JPEG) {
//            GstCaps *gl_caps = gst_caps_new_simple(
//                    "video/x-raw",
//                    "format", G_TYPE_STRING, "RGBA",
//                    "memory", G_TYPE_STRING, "GLMemory",
//                    "texture-target", G_TYPE_STRING, "external-oes",
//                    NULL
//            );
//            g_object_set(leftappsink, "caps", gl_caps, NULL);
//            gst_caps_unref(gl_caps);
//        }
        gst_element_set_name(pipelineLeft_, "pipeline_left");
        gst_element_set_state(pipelineLeft_, GST_STATE_READY);

        bus = gst_element_get_bus(pipelineLeft_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr, nullptr);
        g_source_attach(bus_source, context_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback, pipelineLeft_);
        g_signal_connect(G_OBJECT(leftappsink), "new-sample", (GCallback) newFrameCallback, callbackObj_);
        g_signal_connect(G_OBJECT(leftudpsrc_ident), "handoff", (GCallback) onRtpHeaderMetadata, callbackObj_);
        g_signal_connect(G_OBJECT(leftrtpdepay_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(leftdec_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(leftqueue_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        gst_object_unref(leftudpsrc_ident);
        gst_object_unref(leftrtpdepay_ident);
        gst_object_unref(leftdec_ident);
        gst_object_unref(leftqueue_ident);
        gst_object_unref(leftappsink);
        gst_object_unref(bus);

        GstContext *gst_context_right = gst_context_new("gst.gl.app_context", TRUE);
        gst_structure_set(gst_context_writable_structure(gst_context_right), "context", GST_TYPE_GL_CONTEXT, glContext_, NULL);
        gst_element_set_context(pipelineRight_, gst_context_right);
        gst_context_unref(gst_context_right);

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

        GstElement *h265parse = gst_bin_get_by_name(GST_BIN(pipelineRight_), "h265parse");
        GstPad *padparse = gst_element_get_static_pad(h265parse, "src");
        gst_pad_add_probe(padparse, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, printCapsProbe, NULL, NULL);

        GstElement *rightappsink = gst_bin_get_by_name(GST_BIN(pipelineRight_), "appsink");
//        if (config.codec != Codec::JPEG) {
//            GstCaps *gl_caps2 = gst_caps_new_simple(
//                    "video/x-raw",
//                    "format", G_TYPE_STRING, "RGBA",
//                    "memory", G_TYPE_STRING, "GLMemory",
//                    "texture-target", G_TYPE_STRING, "external-oes",
//                    NULL
//            );
//            g_object_set(rightappsink, "caps", gl_caps2, NULL);
//            gst_caps_unref(gl_caps2);
//        }
        gst_element_set_name(pipelineRight_, "pipeline_right");
        gst_element_set_state(pipelineRight_, GST_STATE_READY);

        bus = gst_element_get_bus(pipelineRight_);
        bus_source = gst_bus_create_watch(bus);
        g_source_set_callback(bus_source, (GSourceFunc) gst_bus_async_signal_func, nullptr, nullptr);
        g_source_attach(bus_source, context_);
        g_source_unref(bus_source);

        g_signal_connect(G_OBJECT(bus), "message::info", (GCallback) infoCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::warning", (GCallback) warningCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::error", (GCallback) errorCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(bus), "message::state-changed", (GCallback) stateChangedCallback, pipelineRight_);
        g_signal_connect(G_OBJECT(rightappsink), "new-sample", (GCallback) newFrameCallback, callbackObj_);
        g_signal_connect(G_OBJECT(rightudpsrc_ident), "handoff", (GCallback) onRtpHeaderMetadata, callbackObj_);
        g_signal_connect(G_OBJECT(rightrtpdepay_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(rightdec_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        g_signal_connect(G_OBJECT(rightqueue_ident), "handoff", (GCallback) onIdentityHandoff, callbackObj_);
        gst_object_unref(rightudpsrc_ident);
        gst_object_unref(rightrtpdepay_ident);
        gst_object_unref(rightdec_ident);
        gst_object_unref(rightqueue_ident);
        gst_object_unref(rightappsink);
        gst_object_unref(bus);

        gst_element_set_state(pipelineLeft_, GST_STATE_PLAYING);
        gst_element_set_state(pipelineRight_, GST_STATE_PLAYING);
    }

    threadPool.detach_task([&]() {
        /* Create a GLib Main Loop and set it to run */
        LOG_INFO("GStreamer entering the main loop");
        mainLoop_ = g_main_loop_new(context_, FALSE);

        g_main_loop_run(mainLoop_);
        LOG_INFO("GStreamer exited the main loop");
        g_main_loop_unref(mainLoop_);
        mainLoop_ = nullptr;
    });
}

GstFlowReturn GstreamerPlayer::newFrameCallback(GstElement *sink, GStreamerCallbackObj *callbackObj) {
    GstSample *sample;
    /* Retrieve the buffer */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    LOG_INFO("NEW_SAMPLE - sample arrived");
    if (sample) {
        if (std::string(sink->object.parent->name) == "pipeline_combined") {
            //LOG_INFO("New GStreamer combined frame received");
            auto pair = callbackObj->first;

            GstBuffer *buffer;
            GstMapInfo mapInfo{};

            buffer = gst_sample_get_buffer(sample);
            gst_buffer_map(buffer, &mapInfo, GST_MAP_READ);

            memcpy(pair->first.dataHandle, mapInfo.data, pair->first.memorySize);
            //LOG_INFO("frame size: %lu. Should be: %lu", mapInfo.size, pair->first.memorySize + pair->second.memorySize);
            memcpy(pair->second.dataHandle, mapInfo.data + pair->first.memorySize - 3, pair->second.memorySize);

            gst_sample_unref(sample);
            gst_buffer_unmap(buffer, &mapInfo);
        } else {
            bool isLeftCamera = std::string(sink->object.parent->name) == "pipeline_left";
            if (isLeftCamera) {
                LOG_INFO("NEW_SAMPLE - New GStreamer frame received from left camera");
            } else {
                LOG_INFO("NEW_SAMPLE - New GStreamer frame received from right camera");
            }

            auto pair = callbackObj->first;
            auto frame = isLeftCamera ? pair->first : pair->second;

            frame.stats->prevTimestamp = frame.stats->currTimestamp;
            frame.stats->currTimestamp = callbackObj->second->GetCurrentTimeUs();
            double diff = frame.stats->currTimestamp - frame.stats->prevTimestamp;
            frame.stats->fps = 1e6f / diff;

            GstBuffer *buffer;
            GstMapInfo mapInfo{};

            buffer = gst_sample_get_buffer(sample);
            GstMemory *memory = gst_buffer_peek_memory(buffer, 0);

            if (gst_is_gl_memory(memory)) {
                GstGLMemory *gl_mem = GST_GL_MEMORY_CAST(memory);
                GLuint tex_id = gst_gl_memory_get_texture_id(gl_mem);
                GLenum tex_target = gst_gl_memory_get_texture_target(gl_mem);
                LOG_INFO("NEW_SAMPLE - it is an gl memory sample");
                return GST_FLOW_OK;
            }
            LOG_INFO("NEW_SAMPLE - it is not an gl memory sample");

            gst_buffer_map(buffer, &mapInfo, GST_MAP_READ);

            memcpy(frame.dataHandle, mapInfo.data, frame.memorySize);

            gst_sample_unref(sample);
            gst_buffer_unmap(buffer, &mapInfo);
        }
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
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
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 1, &myInfoBuf, &size_64) != 0) {
        stats->vidConv = *(static_cast<uint64_t *>(myInfoBuf));
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 2, &myInfoBuf, &size_64) != 0) {
        stats->enc = *(static_cast<uint64_t *>(myInfoBuf));
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 3, &myInfoBuf, &size_64) != 0) {
        stats->rtpPay = *(static_cast<uint64_t *>(myInfoBuf));
    }
    if (gst_rtp_buffer_get_extension_twobytes_header(&rtp_buf, &appbits, 1, 4, &myInfoBuf, &size_64) != 0) {
        stats->rtpPayTimestamp = *(static_cast<uint64_t *>(myInfoBuf));
    }
    gst_rtp_buffer_unmap(&rtp_buf);

    // This is so the last packet of rtp gets saved
    stats->udpSrcTimestamp = ntpTimer->GetCurrentTimeUs();
    stats->udpStream = stats->udpSrcTimestamp - stats->rtpPayTimestamp;
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

    } else if (std::string(identity->object.name) == "dec_ident") {
        stats->decTimestamp = ntpTimer->GetCurrentTimeUs();
        stats->dec = stats->decTimestamp - stats->rtpDepayTimestamp;

    } else if (std::string(identity->object.name) == "queue_ident") {
        stats->queueTimestamp = ntpTimer->GetCurrentTimeUs();
        stats->queue = stats->queueTimestamp - stats->decTimestamp;
        stats->totalLatency = stats->vidConv + stats->enc + stats->rtpPay + stats->udpStream + stats->rtpDepay + stats->dec + stats->queue;
        /*LOG_INFO(
                "Pipeline latencies: vidconv: %lu, enc: %lu, rtpPay: %lu, udpStream: %lu, rtpDepay: %lu, dec: %lu, queue: %lu, total: %lu",
                (unsigned long) stats->vidConv, (unsigned long) stats->enc,
                (unsigned long) stats->rtpPay, (unsigned long) stats->udpStream,
                (unsigned long) stats->rtpDepay, (unsigned long) stats->dec,
                (unsigned long) stats->queue,
                (unsigned long) stats->totalLatency);*/
    }
}

void GstreamerPlayer::stateChangedCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

    LOG_INFO("GStreamer element %s state changed to: %s", GST_MESSAGE_SRC(msg)->name, gst_element_state_get_name(new_state));
}

void GstreamerPlayer::infoCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_info(msg, &err, &debug_info);

    LOG_INFO("GStreamer info received from element: %s, %s", GST_OBJECT_NAME(msg->src), err->message);

    gst_element_set_state(pipeline, GST_STATE_NULL);
}

void GstreamerPlayer::warningCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_warning(msg, &err, &debug_info);

    LOG_INFO("GStreamer warning received from element: %s, %s", GST_OBJECT_NAME(msg->src), err->message);

    gst_element_set_state(pipeline, GST_STATE_NULL);
}

void GstreamerPlayer::errorCallback(GstBus *bus, GstMessage *msg, GstElement *pipeline) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_error(msg, &err, &debug_info);

    LOG_ERROR("GStreamer error received from element: %s, %s", GST_OBJECT_NAME(msg->src), err->message);

    gst_element_set_state(pipeline, GST_STATE_NULL);
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
        LOG_INFO("No decoders found in the GStreamer registry.");
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
            LOG_INFO("HW Decoder: %s (%s)", name, longname);
        } else {
            LOG_INFO("SW Decoder: %s (%s)", name, longname);
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

        LOG_INFO("Element: %s — %s (%s)\n", name, longname, klass);
    }

    gst_plugin_feature_list_free(features);
}
