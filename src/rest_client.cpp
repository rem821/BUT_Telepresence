//
// Created by standa on 27.08.24.
//
#include <nlohmann/json.hpp>
#include "rest_client.h"

using json = nlohmann::json;

int RestClient::StartStream(const StreamingConfig &config) {
    std::string req = json{{"bitrate",          "400k"},
                           {"codec",            "JPEG"},
                           {"encoding_quality", config.encodingQuality},
                           {"fps",              config.fps},
                           {"ip_address",       config.ip},
                           {"port_left",        config.portLeft},
                           {"port_right",       config.portRight},
                           {"resolution",       {{"height", config.verticalResolution}, {"width", config.horizontalResolution}}},
                           {"video_mode",       config.videoMode == VideoMode::STEREO ? "stereo"
                                                                                      : "mono"}}.dump();

    httpClient_.Post("/api/v1/stream/start", req, "application/json");
    return 0;
}

int RestClient::StopStream() {
    httpClient_.Post("/api/v1/stream/stop");
    return 0;
}

StreamingConfig RestClient::GetStreamingConfig() {
    auto conf = StreamingConfig();
    if (auto result = httpClient_.Get("/api/v1/stream/state")) {
        std::string body = result->body;
        auto parsedBody = json::parse(body);

        parsedBody["ip_address"].get_to(conf.ip);
        parsedBody["port_left"].get_to(conf.portLeft);
        parsedBody["port_right"].get_to(conf.portRight);
        conf.codec = Codec::JPEG;
        parsedBody["encoding_quality"].get_to(conf.encodingQuality);
        conf.bitrate = 400;
        parsedBody["resolution"]["width"].get_to(conf.horizontalResolution);
        parsedBody["resolution"]["height"].get_to(conf.verticalResolution);
        conf.videoMode =
                parsedBody["video_mode"].template get<std::string>() == "stereo" ? VideoMode::STEREO
                                                                                 : VideoMode::MONO;
        parsedBody["fps"].get_to(conf.fps);
    }

    return conf;
}

int RestClient::UpdateStreamingConfig(const StreamingConfig &config) {
    std::string req = json{{"bitrate",          "400k"},
                           {"codec",            "JPEG"},
                           {"encoding_quality", config.encodingQuality},
                           {"fps",              config.fps},
                           {"ip_address",       config.ip},
                           {"port_left",        config.portLeft},
                           {"port_right",       config.portRight},
                           {"resolution",       {{"height", config.verticalResolution}, {"width", config.horizontalResolution}}},
                           {"video_mode",       config.videoMode == VideoMode::STEREO ? "stereo"
                                                                                      : "mono"}}.dump();
    httpClient_.Put("/api/v1/stream/update", req, "application/json");
    return 0;
}