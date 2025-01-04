//
// Created by stand on 16.04.2023.
//
#include <cstring>
#include <bitset>
#include <sstream>
#include "pose_server.h"

constexpr int RESPONSE_TIMEOUT_US = 50000;
constexpr int RESPONSE_MIN_BYTES = 6;

constexpr unsigned char IDENTIFIER_1 = 0x47;
constexpr unsigned char IDENTIFIER_2 = 0x54;

constexpr int32_t AZIMUTH_MAX_VALUE = 1'200'000'000;
constexpr int32_t AZIMUTH_MIN_VALUE = -500'000'000;

constexpr int32_t ELEVATION_MAX_VALUE = 1'000'000'000;
constexpr int32_t ELEVATION_MIN_VALUE = 100'000;

PoseServer::PoseServer()
        : socket_(socket(AF_INET, SOCK_DGRAM, 0)), trigger_(false),
          clientAddrLen_(sizeof(clientAddr_)) {

    if (socket_ < 0) {
        LOG_ERROR("Socket creation failed");
        return;
    }

    memset(&myAddr_, 0, sizeof(myAddr_));
    myAddr_.sin_family = AF_INET;
    myAddr_.sin_addr.s_addr = INADDR_ANY;
    myAddr_.sin_port = htons(31285);  // Listen for "now" command on port 31285

    if (bind(socket_, (sockaddr *) &myAddr_, sizeof(myAddr_)) < 0) {
        LOG_ERROR("Bind socket failed");
    }

    // Start listening for the "now" message in a separate thread
    std::thread(&PoseServer::listenForTrigger, this).detach();
    // Start processing the queue in a separate thread
    std::thread(&PoseServer::processQueue, this).detach();
}

// Method to listen for the trigger on port 31285
void PoseServer::listenForTrigger() {
    while (true) {
        char buffer[1024];
        ssize_t received = recvfrom(socket_, buffer, sizeof(buffer) - 1, 0,
                                    (sockaddr *) &clientAddr_, &clientAddrLen_);
        commStart_ = getCurrentUs();

        if (received > 0) {
            buffer[received] = '\0';  // Null-terminate the received data
            std::string message(buffer);

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                trigger_ = true;
            }
            cv_.notify_one();  // Wake up the queue processor
        }
    }
}

// Queue processor: processes one task per trigger
void PoseServer::processQueue() {
    while (true) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        cv_.wait(lock, [this]() { return trigger_ && !taskQueue_.empty(); });

        if (!taskQueue_.empty()) {
            auto task = taskQueue_.top();
            taskQueue_.pop();
            trigger_ = false;  // Reset trigger for next "now" command
            lock.unlock();

            // Execute the highest priority task
            task.second();
        } else {
            LOG_ERROR("Task queue was empty!");
        }
    }
}

// Send message to the client address
void PoseServer::sendMessage(const std::vector<unsigned char> &message) {
    if (sendto(socket_, message.data(), message.size(), 0, (sockaddr *) &clientAddr_,
               clientAddrLen_) < 0) {
        LOG_ERROR("Failed to send message to client address");
    }
    uint64_t commPrevEnd = commEnd_;
    commEnd_ = getCurrentUs();
    float fps = 1e6f / float(commEnd_ - commPrevEnd);
    LOG_ERROR("Pose server took %f to respond. Running with %f FPS", (commEnd_ - commStart_)/1000.0, fps);
}

// Schedule methods by priority
void PoseServer::resetErrors() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    taskQueue_.emplace(RESET_ERRORS, [this]() {
        std::vector<unsigned char> buffer = {IDENTIFIER_1, IDENTIFIER_2, Operation::WRITE,
                                             MessageGroup::ENABLE_AZIMUTH, MessageElement::ENABLE,
                                             0x08, 0x00, 0x00, 0x00,
                                             Operation::WRITE, MessageGroup::ENABLE_ELEVATION,
                                             MessageElement::ENABLE, 0x08, 0x00, 0x00, 0x00};
        sendMessage(buffer);
    });
}

void PoseServer::enableServos(bool enable) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    taskQueue_.emplace(ENABLE_SERVOS, [this, enable]() {
        unsigned char en = enable ? 0x01 : 0x00;
        std::vector<unsigned char> enableBuffer = {IDENTIFIER_1, IDENTIFIER_2, Operation::WRITE,
                                                   MessageGroup::ENABLE_AZIMUTH,
                                                   MessageElement::ENABLE,
                                                   en, 0x00, 0x00, 0x00,
                                                   Operation::WRITE, MessageGroup::ENABLE_ELEVATION,
                                                   MessageElement::ENABLE, en, 0x00, 0x00, 0x00};
        sendMessage(enableBuffer);
    });
}

void PoseServer::setPoseAndSpeed(XrQuaternionf quatPose, int32_t speed) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    taskQueue_.emplace(SET_POSE_AND_SPEED, [this, quatPose, speed]() {
        auto azimuthElevation = quaternionToAzimuthElevation(quatPose);

        auto azimuth_max_side = int32_t((int64_t(AZIMUTH_MAX_VALUE) - AZIMUTH_MIN_VALUE) / 2);
        auto azimuth_center = AZIMUTH_MAX_VALUE - azimuth_max_side;

        auto elevation_max_side = int32_t((int64_t(ELEVATION_MAX_VALUE) - ELEVATION_MIN_VALUE) / 2);
        auto elevation_center = ELEVATION_MAX_VALUE - elevation_max_side;

        auto azimuth = int32_t(
                ((azimuthElevation.azimuth * 2.0F) / M_PI) * azimuth_max_side + azimuth_center);
        auto elevation = int32_t(
                ((-azimuthElevation.elevation * 2.0F) / M_PI) * elevation_max_side +
                elevation_center);

        azimuth *= 1.5f;
        elevation *= 1.5f;

        if (azimuth < AZIMUTH_MIN_VALUE) {
            azimuth = AZIMUTH_MIN_VALUE;
        }
        if (azimuth > AZIMUTH_MAX_VALUE) {
            azimuth = AZIMUTH_MAX_VALUE;
        }
        if (elevation < ELEVATION_MIN_VALUE) {
            elevation = ELEVATION_MIN_VALUE;
        }
        if (elevation > ELEVATION_MAX_VALUE) {
            elevation = ELEVATION_MAX_VALUE;
        }

        int32_t azRevol = 0;
        int32_t elRevol = 0;
        if (azimuth < 0) {
            azRevol = -1;
        }
        if (elevation < 0) {
            elRevol = -1;
        }

        auto azAngleBytes = serializeLEInt(azimuth);
        auto azRevolBytes = serializeLEInt(azRevol);
        auto elAngleBytes = serializeLEInt(elevation);
        auto elRevolBytes = serializeLEInt(elRevol);

        auto speedBytes = serializeLEInt(speed);

        std::vector<unsigned char> const buffer = {IDENTIFIER_1, IDENTIFIER_2,
                                                   Operation::WRITE_CONTINUOS,
                                                   MessageGroup::AZIMUTH, MessageElement::ANGLE,
                                                   0x02,
                                                   azAngleBytes[0], azAngleBytes[1],
                                                   azAngleBytes[2], azAngleBytes[3],
                                                   azRevolBytes[0], azRevolBytes[1],
                                                   azRevolBytes[2], azRevolBytes[3],
                                                   Operation::WRITE_CONTINUOS,
                                                   MessageGroup::ELEVATION, MessageElement::ANGLE,
                                                   0x02,
                                                   elAngleBytes[0], elAngleBytes[1],
                                                   elAngleBytes[2], elAngleBytes[3],
                                                   elRevolBytes[0], elRevolBytes[1],
                                                   elRevolBytes[2], elRevolBytes[3],
                                                   Operation::WRITE,
                                                   MessageGroup::AZIMUTH, MessageElement::SPEED,
                                                   speedBytes[0], speedBytes[1], speedBytes[2],
                                                   speedBytes[3],
                                                   Operation::WRITE,
                                                   MessageGroup::ELEVATION, MessageElement::SPEED,
                                                   speedBytes[0], speedBytes[1], speedBytes[2],
                                                   speedBytes[3],
                                                   Operation::WRITE,
                                                   MessageGroup::ENABLE_AZIMUTH,
                                                   MessageElement::ENABLE,
                                                   0x01, 0x00, 0x00, 0x00,
                                                   Operation::WRITE,
                                                   MessageGroup::ENABLE_ELEVATION,
                                                   MessageElement::ENABLE,
                                                   0x01, 0x00, 0x00, 0x00
        };
        sendMessage(buffer);
    });
}

void PoseServer::setMode() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    taskQueue_.emplace(SET_MODE, [this]() {
        std::vector<unsigned char> modeBuffer = {IDENTIFIER_1, IDENTIFIER_2, Operation::WRITE,
                                                 MessageGroup::AZIMUTH, MessageElement::MODE,
                                                 0x01, 0x00, 0x00, 0x00,
                                                 Operation::WRITE,
                                                 MessageGroup::ELEVATION, MessageElement::MODE,
                                                 0x01, 0x00, 0x00, 0x00};
        sendMessage(modeBuffer);
    });
}

PoseServer::AzimuthElevation PoseServer::quaternionToAzimuthElevation(XrQuaternionf q) {
    double azimuth = 0;
    double elevation = 0;
    double test = q.x * q.y + q.z * q.w;
    if (test > 0.499F) { // Singularity at north pole
        azimuth = 2 * std::atan2(q.x, q.w);
        elevation = 0;
        LOG_INFO("quat x: %2.2f, y: %2.2f, z: %2.2f, w: %2.2f; Azimuth: %2.2f, Elevation: %2.2f",
                 q.x, q.y, q.z, q.w, azimuth, elevation);
        return AzimuthElevation{azimuth, elevation};
    }

    if (test < -0.499F) { // Singularity at south pole
        azimuth = -2 * std::atan2(q.x, q.w);
        elevation = 0;
        LOG_INFO("quat x: %2.2f, y: %2.2f, z: %2.2f, w: %2.2f; Azimuth: %2.2f, Elevation: %2.2f",
                 q.x, q.y, q.z, q.w, azimuth, elevation);
        return AzimuthElevation{azimuth, elevation};
    }

    double sqx = q.x * q.x;
    double sqy = q.y * q.y;
    double sqz = q.z * q.z;
    azimuth = atan2(2 * q.y * q.w - 2 * q.x * q.z, 1 - 2 * sqy - 2 * sqz);
    elevation = atan2(2 * q.x * q.w - 2 * q.y * q.z, 1 - 2 * sqx - 2 * sqz);

    LOG_INFO("quat x: %2.2f, y: %2.2f, z: %2.2f, w: %2.2f; Azimuth: %2.2f, Elevation: %2.2f", q.x,
             q.y, q.z, q.w, azimuth, elevation);
    return AzimuthElevation{azimuth, elevation + 0.5f};
}