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
    if (GetCurrentTimeUsNonAdjusted() - lastSyncedTimestampLocal_ < 1000000) return; // only since once every 1s

    // Create a socket for UDP communication
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        std::cerr << "NTPCLIENT: Error creating socket!" << std::endl;
        return;
    }

    // Configure the server address
    struct sockaddr_in serverAddr{};
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(123);

    // Convert hostname to IP address
    if (inet_pton(AF_INET, ntpServerAddress_.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "NTPCLIENT: Invalid NTP server address!" << std::endl;
        return;
    }

    // Create an NTP request packet (48 bytes)
    uint8_t packet[48] = {0};
    packet[0] = 0b11100011; // LI, Version, Mode

    // Record time before sending the request (T1)
    auto time_sent = std::chrono::high_resolution_clock::now();

    // Send the request packet to the NTP server
    if (sendto(sockfd, packet, sizeof(packet), 0, (struct sockaddr *) &serverAddr,
               sizeof(serverAddr)) < 0) {
        std::cerr << "NTPCLIENT: Error sending request to the NTP server!" << std::endl;
        return;
    }

    // Receive the response packet
    struct sockaddr_in responseAddr{};
    socklen_t responseLen = sizeof(responseAddr);
    if (recvfrom(sockfd, packet, sizeof(packet), 0, (struct sockaddr *) &responseAddr,
                 &responseLen) < 0) {
        std::cerr << "NTPCLIENT: Error receiving response from the NTP server!" << std::endl;
        return;
    }

    // Record time after receiving the response (T4)
    auto time_received = std::chrono::high_resolution_clock::now();

    // Close the socket
    close(sockfd);

    // Calculate the round-trip delay (T4 - T1)
    roundTripDelay_ = std::chrono::duration_cast<std::chrono::microseconds>(
            time_received - time_sent).count();

    uint32_t transmit_time_seconds = ntohl(*(uint32_t *) &packet[40]); // Time seconds since 1900
    uint32_t transmit_time_fraction = ntohl(*(uint32_t *) &packet[44]); // Time fraction part
    double fractionInSeconds = static_cast<double>(transmit_time_fraction) / (1LL << 32); // fraction/2^32
    auto microseconds = static_cast<uint32_t>(fractionInSeconds * 1e6); // Convert fraction of second to microseconds
    uint64_t server_time = uint64_t(transmit_time_seconds) * 1e6 + microseconds - NTP_TIMESTAMP_DELTA * 1e6;
    uint64_t server_time_adj = server_time; //+ (roundTripDelay_ / 2);

    uint64_t local_time = GetCurrentTimeUsNonAdjusted();

    int64_t prevTimeOffset = local_time - localTimeOffset_ - server_time_adj;
    lastSyncedTimestampLocal_ = local_time;
    localTimeOffset_ = local_time - server_time_adj;

    LOG_INFO("NTPCLIENT: Local Time:                               %lu", local_time);
    LOG_INFO("NTPCLIENT: Server Time (with latency correction):    %lu", server_time_adj);
    LOG_INFO("NTPCLIENT: Time offset:                              %ld", prevTimeOffset);
    LOG_INFO("NTPCLIENT: Server Time round trip delay (us):        %lu", roundTripDelay_);
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