//
// Created by stand on 28.07.2025.
//
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <array>
#include <chrono>
#include "pch.h"
#include "log.h"
#include "ntp_timer.h"

using boost::asio::ip::udp;

NtpTimer::NtpTimer(const std::string &ntpServerAddress) : ntpServerAddress_(ntpServerAddress) {};

void NtpTimer::StartAutoSync() {
    LOG_INFO("NTPCLIENT: Starting auto sync with %s every 5 seconds...", ntpServerAddress_.c_str());
    timer_ = std::make_unique<boost::asio::steady_timer>(io_);
    auto syncLoop = std::make_shared<std::function<void()>>();
    *syncLoop = [this, syncLoop]() {
        SyncWithServer(io_);
        timer_->expires_after(std::chrono::seconds(2));
        auto handler = [this, syncLoop](const boost::system::error_code &ec) {
            try {
                if (!ec) {
                    LOG_INFO("NTPCLIENT: Timer triggered, scheduling next sync...");
                    (*syncLoop)();  // safe if alive
                } else {
                    LOG_ERROR("NTPCLIENT: Timer error: %s", ec.message().c_str());
                }
            } catch (const std::exception &e) {
                LOG_ERROR("NTPCLIENT: Exception in timer callback: %s", e.what());
            }
        };
        timer_->async_wait(handler);
    };
    (*syncLoop)();

    // Start io_context in background
    ioThread_ = std::thread([this]() {
        io_.run();
    });
}

void NtpTimer::SyncWithServer(boost::asio::io_context &io) {
    LOG_INFO("NTPCLIENT: Sync underway...");
    std::vector<Sample> goodSamples;

    for (int i = 0; i < 3; ++i) {
        auto result = GetOneNtpSample(io);
        if (result.has_value()) {
            const auto &[offset, rtt] = result.value();
            goodSamples.emplace_back(Sample{offset, rtt});
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    if (goodSamples.empty()) return;

    uint64_t bestRtt = std::numeric_limits<uint64_t>::max();
    uint64_t bestIndex = 0;
    for (int i = 0; i < goodSamples.size(); i++) {
        if (goodSamples[i].rtt < bestRtt) {
            bestRtt = goodSamples[i].rtt;
            bestIndex = i;
        }
    }

    smoothedOffsetUs_ = alpha * goodSamples[bestIndex].offset + (1.0 - alpha) * smoothedOffsetUs_;
    localTimeOffset_ = static_cast<int64_t>(smoothedOffsetUs_);
    lastSyncedTimestampLocal_ = GetCurrentTimeUsNonAdjusted();

    LOG_INFO("NTPCLIENT: Current offset after filtering: %ld us", localTimeOffset_);
}

std::optional<std::pair<int64_t, uint64_t>> NtpTimer::GetOneNtpSample(boost::asio::io_context &io) {
    try {
        LOG_INFO("NTPCLIENT: Fetching NTP sample...");
        udp::resolver resolver(io);
        udp::endpoint serverEndpoint = *resolver.resolve(udp::v4(), ntpServerAddress_, "123").begin();

        udp::socket socket(io);
        socket.open(udp::v4());
        //socket.set_option(boost::asio::socket_base::receive_timeout(std::chrono::seconds(1)));

        std::array<uint8_t, 48> request{};
        request[0] = 0b11100011;  // LI = 3 (unsynchronized), Version = 4, Mode = 3 (client)

        auto timeSent = std::chrono::high_resolution_clock::now();
        socket.send_to(boost::asio::buffer(request), serverEndpoint);

        std::array<uint8_t, 48> response{};
        udp::endpoint senderEndpoint;
        boost::system::error_code ec;

        size_t len = socket.receive_from(boost::asio::buffer(response), senderEndpoint, 0, ec);
        auto timeReceived = std::chrono::high_resolution_clock::now();
        LOG_INFO("NTPCLIENT: Received a response from the NTP server...");

        if (ec || len < 48) {
            LOG_ERROR("NTPCLIENT: Failed to receive NTP response!");
            return std::nullopt;
        }

        uint64_t roundTripDelay = std::chrono::duration_cast<std::chrono::microseconds>(timeReceived - timeSent).count();
        if (roundTripDelay > 20000) {
            LOG_ERROR("NTPCLIENT: High RTT: %lu us — discarding sample", roundTripDelay);
            return std::nullopt;
        }

        uint32_t tx_seconds = ntohl(*reinterpret_cast<uint32_t *>(&response[40]));
        uint32_t tx_fraction = ntohl(*reinterpret_cast<uint32_t *>(&response[44]));

        double fractionInSeconds = static_cast<double>(tx_fraction) / (1LL << 32);
        auto microseconds = static_cast<uint32_t>(fractionInSeconds * 1e6);
        uint64_t serverTime = static_cast<uint64_t>(tx_seconds - NTP_TIMESTAMP_DELTA) * 1'000'000 + microseconds;
        LOG_ERROR("NTPCLIENT: Server time: %lu vs Client time: %lu", serverTime, GetCurrentTimeUsNonAdjusted());

        // latency-compensated server time
        uint64_t serverTimeAdj = serverTime + roundTripDelay / 2;

        // time offset smoothing to eliminate discrete jumps
        const uint64_t now = GetCurrentTimeUsNonAdjusted();
        int64_t newOffset = now - serverTimeAdj;

        LOG_INFO("NTPCLIENT: Received NTP sample: Offset: %ld us | RTT: %lu us", newOffset, roundTripDelay);
        return std::make_pair(newOffset, roundTripDelay);
    } catch (const std::exception &e) {
        LOG_ERROR("NTPCLIENT: Exception during sync: %s", e.what());
        return std::nullopt;
    }
}

uint64_t NtpTimer::GetCurrentTimeUs() const {
    return GetCurrentTimeUsNonAdjusted() - localTimeOffset_;
}

uint64_t NtpTimer::GetCurrentTimeUsNonAdjusted() {
    struct timespec res{};
    clock_gettime(CLOCK_REALTIME, &res);
    return static_cast<uint64_t>(res.tv_sec) * 1'000'000 + res.tv_nsec / 1'000;
}