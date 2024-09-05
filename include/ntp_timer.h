//
// Created by stand on 05.09.2024.
//
#pragma once
#include <string>


class NtpTimer {
public:
    NtpTimer(const std::string& ntpServerAddress);

    uint32_t GetCurrentTimeUs();

private:

    void SyncWithServer();

    std::string ntpServerAddress_;
    uint32_t lastSyncedTimestamp_;
    uint32_t localTimeOffset_;
    uint32_t NTP_TIMESTAMP_DELTA = 2208988800U;
    uint32_t roundTripDelay_;
};