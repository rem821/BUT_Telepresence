//
// Created by stand on 16.04.2023.
//
#pragma once

#include "pch.h"
#include "log.h"
#include <chrono>

#include "BS_thread_pool.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>

class ServoCommunicator {
    enum Operation {
        READ = 0x01,
        WRITE = 0x02,
        WRITE_CONTINUOUS = 0x04,
    };

    enum MessageGroup {
        ENABLE_ELEVATION = 0x11,
        ENABLE_AZIMUTH = 0x12,
        ELEVATION = 0x19,
        AZIMUTH = 0x1A,
    };

    enum MessageElement {
        ENABLE = 0x00,
        ACCELERATION = 0x00,
        DECELERATION = 0x01,
        ANGLE = 0x04,
        SPEED = 0x07,
        MODE = 0x09,
    };

    struct AzimuthElevation {
        double azimuth;
        double elevation;
    };

public:

    explicit ServoCommunicator(BS::thread_pool<BS::tp::none> &threadPool, StreamingConfig &config);

    ~ServoCommunicator() = default;

    [[nodiscard]] bool isInitialized() const { return isInitialized_; };

    [[nodiscard]] bool isReady() const { return isReady_; };

    [[nodiscard]] bool servosEnabled() const { return servosEnabled_; }

    void resetErrors(BS::thread_pool<BS::tp::none> &threadPool);

    void enableServos(bool enable, BS::thread_pool<BS::tp::none> &threadPool);

    void setPoseAndSpeed(XrQuaternionf quatPose, int32_t speed, BS::thread_pool<BS::tp::none> &threadPool);

    void sendOdinControlPacket(float linSpeedX, float linSpeedY, float angSpeed, BS::thread_pool<BS::tp::none> &threadPool);

private:

    [[nodiscard]] bool checkReadiness() const;

    void setMode(BS::thread_pool<BS::tp::none> &threadPool);

    void sendMessage(const std::vector<unsigned char> &message);

    bool waitForResponse(const std::vector<uint32_t> &statusBytes);

    template<typename IntType>
    [[nodiscard]] inline static std::vector<uint8_t> serializeLEInt(const IntType &value) {
        std::vector<uint8_t> data{};
        for (size_t nBytes = 0; nBytes <= (sizeof(IntType) - 1); ++nBytes) {
            data.push_back((value >> nBytes * 8) & 0xFF);
        }

        return data;
    }

    template<typename FloatType>
    [[nodiscard]] inline static std::vector<uint8_t> serializeLEFloat(const FloatType &value) {
        uint64_t shadow = 0;
        // Create a vector to hold the serialized bytes
        memcpy(&shadow, &value, sizeof(FloatType));

        std::vector<uint8_t> data;
        data = serializeLEInt<uint32_t>(shadow);

        return data;
    }

    static AzimuthElevation quaternionToAzimuthElevation(XrQuaternionf quat);

    bool isInitialized_ = false;
    bool isReady_ = false;
    bool servosEnabled_ = false;

    int32_t frameId_ = 0;

    int socket_ = -1;
    sockaddr_in myAddr_{}, destAddr_{};
    std::future<void> threadFuture_;

    std::chrono::time_point<std::chrono::high_resolution_clock> timestamp_;
};