//
// Created by standa on 27.08.24.
//
#include "pch.h"
#include "common.h"
#include "httplib.h"

#pragma once

class RestClient {
public:
    RestClient() {};

    int StartStream(const StreamingConfig& config);

    int StopStream();

    StreamingConfig GetStreamingConfig();

    int UpdateStreamingConfig(const StreamingConfig& config);

private:

    std::string API_IP = "192.168.1.105";
    int API_PORT = 8080;
    httplib::Client httpClient_{API_IP, API_PORT};
};