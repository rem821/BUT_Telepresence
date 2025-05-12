//
// Created by stand on 05.09.2024.
//
#include "log.h"
#include "ntp_timer.h"

NtpTimer::NtpTimer(const std::string &ntpServerAddress) {
    ntpServerAddress_ = ntpServerAddress;
    SyncWithServer();
}


void NtpTimer::SyncWithServer() {
    if (GetCurrentTimeUsNonAdjusted() - lastSyncedTimestampLocal_ < 1000000 || !ntpAddressValid_) {
        return; // Only sync once per second
    }

    uint64_t local_time = GetCurrentTimeUsNonAdjusted();
    lastSyncedTimestampLocal_ = local_time;

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        LOG_ERROR("NTPCLIENT: Error creating socket!");
        return;
    }

    // Set a timeout for receiving data - 1s
    struct timeval timeout{};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        LOG_ERROR("NTPCLIENT: Failed to set socket timeout!");
        close(sockfd);
        return;
    }

    struct sockaddr_in serverAddr{};
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(123);

    if (inet_pton(AF_INET, ntpServerAddress_.c_str(), &serverAddr.sin_addr) <= 0) {
        LOG_ERROR("NTPCLIENT: Invalid NTP server address!");
        close(sockfd);
        return;
    }

    uint8_t packet[48] = {0};
    packet[0] = 0b11100011; // LI, Version, Mode

    auto time_sent = std::chrono::high_resolution_clock::now();

    if (sendto(sockfd, packet, sizeof(packet), 0, reinterpret_cast<struct sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
        LOG_ERROR("NTPCLIENT: Error sending request to the NTP server!");
        close(sockfd);
        return;
    }

    struct sockaddr_in responseAddr{};
    socklen_t responseLen = sizeof(responseAddr);
    ssize_t recv_len = recvfrom(sockfd, packet, sizeof(packet), 0, reinterpret_cast<struct sockaddr *>(&responseAddr), &responseLen);
    if (recv_len < 0) {
        LOG_ERROR("NTPCLIENT: Error receiving response (server might be down)!");
        ntpAddressValid_ = false;
        close(sockfd);
        return;
    }

    auto time_received = std::chrono::high_resolution_clock::now();
    close(sockfd);

    // Process packet
    roundTripDelay_ = std::chrono::duration_cast<std::chrono::microseconds>(
            time_received - time_sent).count();

    uint32_t transmit_time_seconds = ntohl(*(uint32_t *) &packet[40]);
    uint32_t transmit_time_fraction = ntohl(*(uint32_t *) &packet[44]);

    double fractionInSeconds = static_cast<double>(transmit_time_fraction) / (1LL << 32);
    auto microseconds = static_cast<uint32_t>(fractionInSeconds * 1e6);
    uint64_t server_time = static_cast<uint64_t>(transmit_time_seconds) * 1e6 + microseconds
                           - static_cast<uint64_t>(NTP_TIMESTAMP_DELTA) * 1e6;

    uint64_t server_time_adj = server_time; // Add (roundTripDelay_ / 2) if you want latency compensation

    int64_t prevTimeOffset = local_time - localTimeOffset_ - server_time_adj;
    localTimeOffset_ = local_time - server_time_adj;

    //LOG_INFO("NTPCLIENT: Local Time:                               %lu", local_time);
    //LOG_INFO("NTPCLIENT: Server Time (with latency correction):    %lu", server_time_adj);
    //LOG_INFO("NTPCLIENT: Time offset:                              %ld", prevTimeOffset);
    //LOG_INFO("NTPCLIENT: Server Time round trip delay (us):        %lu", roundTripDelay_);
}

uint64_t NtpTimer::GetCurrentTimeUs() const {
    return GetCurrentTimeUsNonAdjusted() - localTimeOffset_;
}

uint64_t NtpTimer::GetCurrentTimeUsNonAdjusted() {
    struct timespec res{};
    clock_gettime(CLOCK_REALTIME, &res);
    uint64_t us = 1e6 * res.tv_sec + (uint64_t) res.tv_nsec / 1e3;
    return us;
}