//
// Created by stand on 05.09.2024.
//
#pragma once
#include "pch.h"


class NtpTimer {
public:
    NtpTimer(const std::string& ntpServerAddress);

    void SyncWithServer();

    [[nodiscard]] uint64_t GetCurrentTimeUs() const;

private:

    static uint64_t GetCurrentTimeUsNonAdjusted();

    std::string ntpServerAddress_;
    bool ntpAddressValid_ = true;
    uint64_t lastSyncedTimestampLocal_;
    int64_t localTimeOffset_;
    uint64_t NTP_TIMESTAMP_DELTA = 2208988800U;
    uint64_t roundTripDelay_;
};