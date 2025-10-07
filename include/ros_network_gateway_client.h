//
// Created by standa on 10/6/25.
//
#pragma once

#include "pch.h"
#include "log.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include "BS_thread_pool.hpp"

class RosNetworkGatewayClient {

public:
    explicit RosNetworkGatewayClient();

private:

    void listenForMessages();
    static bool parseMessage(const std::vector<uint8_t>& buffer,
                                               double& timestamp,
                                               std::string& topic,
                                               std::string& type,
                                               std::string& payload);
    sockaddr_in myAddr_{}, serverAddr_{};
    socklen_t serverAddrLen_;
    int socket_ = -1;
};