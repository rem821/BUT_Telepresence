//
// Created by stand on 05.09.2024.
//
#include <iostream>
#include <cstring>
#include <ctime>
#include <chrono>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "ntp_timer.h"

NtpTimer::NtpTimer(const std::string &ntpServerAddress) {
    ntpServerAddress_ = ntpServerAddress;
    SyncWithServer();
}


void NtpTimer::SyncWithServer() {
    // Create a socket for UDP communication
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        std::cerr << "Error creating socket!" << std::endl;
        return;
    }

    // Configure the server address
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(123);

    // Convert hostname to IP address
    if (inet_pton(AF_INET, ntpServerAddress_.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid NTP server address!" << std::endl;
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
        std::cerr << "Error sending request to the NTP server!" << std::endl;
        return;
    }

    // Receive the response packet
    struct sockaddr_in responseAddr;
    socklen_t responseLen = sizeof(responseAddr);
    if (recvfrom(sockfd, packet, sizeof(packet), 0, (struct sockaddr *) &responseAddr,
                 &responseLen) < 0) {
        std::cerr << "Error receiving response from the NTP server!" << std::endl;
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
    double fractionInSeconds =
            static_cast<double>(transmit_time_fraction) / (1LL << 32); // fraction/2^32
    uint32_t microseconds = static_cast<uint32_t>(fractionInSeconds *
                                                  1e6); // Convert fraction of second to microseconds
    uint32_t t = transmit_time_seconds * 1e6 + microseconds;

    // Convert seconds to Unix epoch time
    uint32_t server_time = transmit_time_seconds - NTP_TIMESTAMP_DELTA * 1e6;
    uint32_t local_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    lastSyncedTimestamp_ = server_time;
    localTimeOffset_ = local_time - server_time;

    // Print the original server time without latency correction
    std::cout << "Server Time (without latency correction): " << std::endl;
    //printCurrentTime(transmit_time_seconds);

    // Adjust the time for latency (round trip time / 2)
    server_time += (roundTripDelay_ / 2) / 1000; // Adjust for microseconds

    // Print the adjusted time with latency compensation
    std::cout << "Server Time (with latency correction): " << std::endl;
    //printCurrentTime(server_time);

    return;
}

uint32_t NtpTimer::GetCurrentTimeUs() {
    uint32_t local_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    //TODO: When to re-sync? How quickly the time drifts away?
    return local_time - localTimeOffset_;
}