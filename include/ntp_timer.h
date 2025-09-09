//
// Created by stand on 28.07.2025.
//
#pragma once
#include <string>
#include <cstdint>
#include <tuple>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

class NtpTimer {
public:
    explicit NtpTimer(const std::string& ntpServerAddress);

    void StartAutoSync();

    [[nodiscard]] uint64_t GetCurrentTimeUs() const;

private:
    void SyncWithServer(boost::asio::io_context& io);

    std::optional<std::pair<int64_t, uint64_t>> GetOneNtpSample(boost::asio::io_context& io);

    static uint64_t GetCurrentTimeUsNonAdjusted();

    struct Sample {
        int64_t offset;
        uint64_t rtt;
    };

    std::string ntpServerAddress_;

    double smoothedOffsetUs_ = 0;

    uint64_t lastSyncedTimestampLocal_ = 0;
    int64_t localTimeOffset_ = 0;

    static constexpr uint32_t NTP_TIMESTAMP_DELTA = 2208988800U;
    static constexpr double alpha = 0.1;

    boost::asio::io_context io_;
    std::unique_ptr<boost::asio::steady_timer> timer_;
    std::thread ioThread_;
};