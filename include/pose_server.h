//
// Created by stand on 16.04.2023.
//
#pragma once

#include "pch.h"
#include "log.h"
#include <queue>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <iostream>
#include <mutex>
#include <condition_variable>

#include <sys/socket.h>
#include <arpa/inet.h>

#include "ntp_timer.h"

class PoseServer {
    // Define priority levels
    enum Priority {FRAME_LATENCY = 0, SET_MODE = 1, RESET_ERRORS = 2, ENABLE_SERVOS = 3, SET_POSE_AND_SPEED = 4};

    template<typename T>
    class MessagePriorityQueue {
    public:
        void emplace(int priority, const T& value) {
            // Insert or update the element with the given priority
            elements[priority] = value;
        }

        std::pair<int, T> top() {
            if (elements.empty()) {
                throw std::out_of_range("Priority queue is empty");
            }
            // The highest priority is the last element in the map
            auto it = std::prev(elements.end());
            return *it;
        }

        void pop() {
            if (!elements.empty()) {
                elements.erase(std::prev(elements.end()));
            }
        }

        bool empty() const {
            return elements.empty();
        }

    private:
        std::map<int, T> elements;
    };

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

    explicit PoseServer(NtpTimer *ntpTimer, HUDState* hudState);

    ~PoseServer() = default;

    void resetErrors();
    void enableServos(bool enable);
    void setPoseAndSpeed(XrQuaternionf quatPose, int32_t speed, RobotMovementRange movementRange, bool azimuthElevationReversed);
    void setMode();

    //Set an additional debugging message to be sent to the client containing latency info
    void setFrameLatencyMessage(const CameraStats cameraStats);

private:

    void sendMessage(const std::vector<unsigned char> &message);

    void listenForTrigger();

    void processQueue();

    void parseTeleoperationState(std::string& state);

    template<typename IntType>
    [[nodiscard]] inline static std::vector<uint8_t> serializeLEInt(const IntType &value) {
        std::vector<uint8_t> data{};
        for (size_t nBytes = 0; nBytes <= (sizeof(IntType) - 1); ++nBytes) {
            data.push_back((value >> nBytes * 8) & 0xFF);
        }

        return data;
    }

    static AzimuthElevation quaternionToAzimuthElevation(XrQuaternionf quat);

    NtpTimer* ntpTimer_;
    HUDState* hudState_;

    int socket_;
    struct sockaddr_in myAddr_, clientAddr_;
    socklen_t clientAddrLen_;
    std::atomic<bool> trigger_;
    std::mutex queueMutex_;
    std::condition_variable cv_;

    int32_t frameId_ = 0;

    int32_t azimuthFiltered, elevationFiltered;
    float filterAlpha = 0.2f;

    MessagePriorityQueue<std::function<void()>> taskQueue_;

    uint64_t commStart_{}, commEnd_{};
};