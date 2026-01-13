//
// Created by stand on 16.04.2023.
//
#include <cstring>
#include <bitset>
#include <sstream>
#include "pose_server.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

constexpr int RESPONSE_TIMEOUT_US = 50000;
constexpr int RESPONSE_MIN_BYTES = 6;

constexpr unsigned char SERVO_COMMAND_MESSAGE_TYPE = 0x01;
constexpr unsigned char LOG_MESSAGE_TYPE = 0x02;
constexpr unsigned char EMPTY_MESSAGE_TYPE = 0x03;
constexpr unsigned char IDENTIFIER_1 = 0x47;
constexpr unsigned char IDENTIFIER_2 = 0x54;

PoseServer::PoseServer(NtpTimer *ntpTimer, HUDState* hudState) : ntpTimer_(ntpTimer), hudState_(hudState), socket_(socket(AF_INET, SOCK_DGRAM, 0)), trigger_(false),
                                             clientAddrLen_(sizeof(clientAddr_)) {
    if (socket_ < 0) {
        LOG_ERROR("PoseServer: Socket creation failed");
        return;
    }

    memset(&myAddr_, 0, sizeof(myAddr_));
    myAddr_.sin_family = AF_INET;
    myAddr_.sin_addr.s_addr = INADDR_ANY;
    myAddr_.sin_port = htons(31285);  // Listen for "now" command on port 31285

    if (bind(socket_, (sockaddr *) &myAddr_, sizeof(myAddr_)) < 0) {
        LOG_ERROR("PoseServer: Bind socket failed");
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
        ssize_t received = recvfrom(socket_, buffer, sizeof(buffer) - 1, 0, (sockaddr *) &clientAddr_, &clientAddrLen_);
        commStart_ = ntpTimer_->GetCurrentTimeUs();

        if (received > 0) {
            buffer[received] = '\0';  // Null-terminate the received data
            std::string message(buffer);
            parseTeleoperationState(message);
            LOG_INFO("PoseServer: Poll signal received");

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
        cv_.wait(lock, [this]() { return trigger_.load(); });

        trigger_ = false;
        if (taskQueue_.empty()) {
            lock.unlock();

            std::vector<unsigned char> emptyMsg = {EMPTY_MESSAGE_TYPE};
            sendMessage(emptyMsg);
            LOG_ERROR("PoseServer: sending empty message");

            continue;
        }

        if (!taskQueue_.empty()) {
            auto task = taskQueue_.top();
            taskQueue_.pop();
            lock.unlock();

            // Execute the highest priority task
            task.second();
        } else {
            LOG_ERROR("PoseServer: Task queue was empty!");
        }
    }
}

// Send message to the client address
void PoseServer::sendMessage(const std::vector<unsigned char> &message) {
    if (sendto(socket_, message.data(), message.size(), 0, (sockaddr *) &clientAddr_, clientAddrLen_) < 0) {
        LOG_ERROR("PoseServer: Failed to send message to client address");
    }
    uint64_t commPrevEnd = commEnd_;
    commEnd_ = ntpTimer_->GetCurrentTimeUs();
    float fps = 1e6f / float(commEnd_ - commPrevEnd);
    LOG_ERROR("PoseServer: took %f to respond. Running with %f FPS", (commEnd_ - commStart_) / 1000.0, fps);
}

// Schedule methods by priority
void PoseServer::resetErrors() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    taskQueue_.emplace(RESET_ERRORS, [this]() {
        std::vector<unsigned char> buffer = {SERVO_COMMAND_MESSAGE_TYPE,
                                             IDENTIFIER_1, IDENTIFIER_2, Operation::WRITE,
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
        std::vector<unsigned char> enableBuffer = {SERVO_COMMAND_MESSAGE_TYPE,
                                                   IDENTIFIER_1, IDENTIFIER_2, Operation::WRITE,
                                                   MessageGroup::ENABLE_AZIMUTH,
                                                   MessageElement::ENABLE,
                                                   en, 0x00, 0x00, 0x00,
                                                   Operation::WRITE, MessageGroup::ENABLE_ELEVATION,
                                                   MessageElement::ENABLE, en, 0x00, 0x00, 0x00};
        sendMessage(enableBuffer);
    });
}

void PoseServer::setPoseAndSpeed(XrQuaternionf quatPose, int32_t speed, RobotMovementRange movementRange, bool azimuthElevationReversed) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    taskQueue_.emplace(SET_POSE_AND_SPEED, [this, quatPose, speed, movementRange, azimuthElevationReversed]() {
        auto azimuthElevation = quaternionToAzimuthElevation(quatPose);

        auto azimuth_max_side = int32_t((int64_t(movementRange.azimuthMax) - movementRange.azimuthMin) / 2);
        auto azimuth_center = movementRange.azimuthMax - azimuth_max_side;

        auto elevation_max_side = int32_t((int64_t(movementRange.elevationMax) - movementRange.elevationMin) / 2);
        auto elevation_center = movementRange.elevationMax - elevation_max_side;

        auto azimuth = int32_t(((azimuthElevation.azimuth * 2.0F) / M_PI) * azimuth_max_side + azimuth_center);
        auto elevation = int32_t(((-azimuthElevation.elevation * 2.0F) / M_PI) * elevation_max_side + elevation_center);

        azimuth += (azimuth - azimuth_center) * movementRange.speedMultiplier;
        elevation += (elevation - elevation_center + 200'000'000) * movementRange.speedMultiplier;

        // Protect filter value access with mutex
        std::vector<uint8_t> azAngleBytes, azRevolBytes, elAngleBytes, elRevolBytes;
        {
            std::lock_guard<std::mutex> lock(filterMutex_);

            azimuthFiltered = azimuthFiltered * (1.0f - filterAlpha) + azimuth * filterAlpha;
            elevationFiltered = elevationFiltered * (1.0f - filterAlpha) + elevation * filterAlpha;

            if (azimuthFiltered < movementRange.azimuthMin) {
                azimuthFiltered = movementRange.azimuthMin;
            }
            if (azimuthFiltered > movementRange.azimuthMax) {
                azimuthFiltered = movementRange.azimuthMax;
            }
            if (elevationFiltered < movementRange.elevationMin) {
                elevationFiltered = movementRange.elevationMin;
            }
            if (elevationFiltered > movementRange.elevationMax) {
                elevationFiltered = movementRange.elevationMax;
            }

            int32_t azRevol = 0;
            int32_t elRevol = 0;
            if (azimuthFiltered < 0) {
                azRevol = -1;
            }
            if (elevationFiltered < 0) {
                elRevol = -1;
            }

            //LOG_INFO("Sending - Azimuth: %d, Elevation: %d", azimuth, elevation);
            azAngleBytes = serializeLEInt(azimuthFiltered);
            azRevolBytes = serializeLEInt(azRevol);
            elAngleBytes = serializeLEInt(elevationFiltered);
            elRevolBytes = serializeLEInt(elRevol);
            if (azimuthElevationReversed) {
                azAngleBytes = serializeLEInt(elevationFiltered);
                azRevolBytes = serializeLEInt(elRevol);
                elAngleBytes = serializeLEInt(azimuthFiltered);
                elRevolBytes = serializeLEInt(azRevol);
            }
        }

        auto speedBytes = serializeLEInt(speed);

        auto frameIdBytes = serializeLEInt(frameId_);
        frameId_++;

        std::vector<unsigned char> const buffer = {SERVO_COMMAND_MESSAGE_TYPE,
                                                   IDENTIFIER_1, IDENTIFIER_2,
                                                   Operation::WRITE_CONTINUOUS,
                                                   MessageGroup::AZIMUTH, MessageElement::ANGLE,
                                                   0x02,
                                                   azAngleBytes[0], azAngleBytes[1], azAngleBytes[2], azAngleBytes[3],
                                                   azRevolBytes[0], azRevolBytes[1], azRevolBytes[2], azRevolBytes[3],
                                                   Operation::WRITE_CONTINUOUS,
                                                   MessageGroup::ELEVATION, MessageElement::ANGLE,
                                                   0x02,
                                                   elAngleBytes[0], elAngleBytes[1], elAngleBytes[2], elAngleBytes[3],
                                                   elRevolBytes[0], elRevolBytes[1], elRevolBytes[2], elRevolBytes[3],
                                                   Operation::WRITE,
                                                   MessageGroup::AZIMUTH, MessageElement::SPEED,
                                                   speedBytes[0], speedBytes[1], speedBytes[2], speedBytes[3],
                                                   Operation::WRITE,
                                                   MessageGroup::ELEVATION, MessageElement::SPEED,
                                                   speedBytes[0], speedBytes[1], speedBytes[2], speedBytes[3],
                                                   Operation::WRITE,
                                                   MessageGroup::ENABLE_AZIMUTH, MessageElement::ENABLE,
                                                   0x01, 0x00, 0x00, 0x00,
                                                   Operation::WRITE,
                                                   MessageGroup::ENABLE_ELEVATION, MessageElement::ENABLE,
                                                   0x01, 0x00, 0x00, 0x00
        };
        sendMessage(buffer);
    });
}

void PoseServer::setMode() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    taskQueue_.emplace(SET_MODE, [this]() {
        std::vector<unsigned char> modeBuffer = {SERVO_COMMAND_MESSAGE_TYPE,
                                                 IDENTIFIER_1, IDENTIFIER_2, Operation::WRITE,
                                                 MessageGroup::AZIMUTH, MessageElement::MODE,
                                                 0x01, 0x00, 0x00, 0x00,
                                                 Operation::WRITE,
                                                 MessageGroup::ELEVATION, MessageElement::MODE,
                                                 0x01, 0x00, 0x00, 0x00};
        sendMessage(modeBuffer);
    });
}

void PoseServer::setFrameLatencyMessage(const CameraStats cameraStats) {
    std::lock_guard<std::mutex> lock(queueMutex_);

    taskQueue_.emplace(FRAME_LATENCY, [this, cameraStats]() {
        auto vidconv = serializeLEInt(int32_t(cameraStats.vidConv));
        auto enc = serializeLEInt(int32_t(cameraStats.enc));
        auto rtppay = serializeLEInt(int32_t(cameraStats.rtpPay));
        auto udpStream = serializeLEInt(int32_t(cameraStats.udpStream));
        auto rtpdepay = serializeLEInt(int32_t(cameraStats.rtpDepay));
        auto dec = serializeLEInt(int32_t(cameraStats.dec));

        std::vector<unsigned char> logBuffer = {LOG_MESSAGE_TYPE,
                                             vidconv[0], vidconv[1], vidconv[2], vidconv[3],
                                             enc[0], enc[1], enc[2], enc[3],
                                             rtppay[0], rtppay[1], rtppay[2], rtppay[3],
                                             udpStream[0], udpStream[1], udpStream[2], udpStream[3],
                                             rtpdepay[0], rtpdepay[1], rtpdepay[2], rtpdepay[3],
                                             dec[0], dec[1], dec[2], dec[3]};
        sendMessage(logBuffer);
    });
}

void PoseServer::parseTeleoperationState(std::string& state) {
    if(state.empty()) return;
    auto parsedState = json::parse(state);

    parsedState["notification"]["title"].get_to(hudState_->notificationTitle);
    parsedState["notification"]["message"].get_to(hudState_->notificationMessage);
    parsedState["notification"]["severity"].get_to(hudState_->notificationSeverity);
    parsedState["teleoperation_state"]["latency"].get_to(hudState_->teleoperationLatency);
    parsedState["teleoperation_state"]["speed"].get_to(hudState_->teleoperatedVehicleSpeed);
    parsedState["teleoperation_state"]["state"].get_to(hudState_->teleoperationState);
}


PoseServer::AzimuthElevation PoseServer::quaternionToAzimuthElevation(XrQuaternionf q) {
    double azimuth = 0;
    double elevation = 0;
    double test = q.x * q.y + q.z * q.w;
    if (test > 0.499F) { // Singularity at north pole
        azimuth = 2 * std::atan2(q.x, q.w);
        elevation = 0;
        //LOG_INFO("quat x: %2.2f, y: %2.2f, z: %2.2f, w: %2.2f; Azimuth: %2.2f, Elevation: %2.2f", q.x, q.y, q.z, q.w, azimuth, elevation);
        return AzimuthElevation{azimuth, elevation};
    }

    if (test < -0.499F) { // Singularity at south pole
        azimuth = -2 * std::atan2(q.x, q.w);
        elevation = 0;
        //LOG_INFO("quat x: %2.2f, y: %2.2f, z: %2.2f, w: %2.2f; Azimuth: %2.2f, Elevation: %2.2f", q.x, q.y, q.z, q.w, azimuth, elevation);
        return AzimuthElevation{azimuth, elevation};
    }

    double sqx = q.x * q.x;
    double sqy = q.y * q.y;
    double sqz = q.z * q.z;
    azimuth = atan2(2 * q.y * q.w - 2 * q.x * q.z, 1 - 2 * sqy - 2 * sqz);
    elevation = atan2(2 * q.x * q.w - 2 * q.y * q.z, 1 - 2 * sqx - 2 * sqz);

//    if (elevation < -M_PI / 2) {
//        elevation = -M_PI / 2;
//    }
//    if (elevation > M_PI / 2) {
//        elevation = M_PI / 2;
//    }

//    if (azimuth < -M_PI / 2) azimuth = -M_PI / 2;
//    if (azimuth > M_PI / 2) azimuth = M_PI / 2;
//

    //LOG_INFO("quat x: %2.2f, y: %2.2f, z: %2.2f, w: %2.2f; Azimuth: %2.2f, Elevation: %2.2f", q.x, q.y, q.z, q.w, azimuth, elevation + 0.5f);
    return AzimuthElevation{azimuth, elevation + 0.5f};
}