//
// Created by stand on 16.04.2023.
//
#include <cstring>
#include <bitset>
#include <sstream>
#include "servo_communicator.h"

constexpr int RESPONSE_TIMEOUT_US = 50000;
constexpr int RESPONSE_MIN_BYTES = 6;

constexpr unsigned char IDENTIFIER_1 = 0x47;
constexpr unsigned char IDENTIFIER_2 = 0x54;

constexpr int32_t AZIMUTH_MAX_VALUE = 1'200'000'000;
constexpr int32_t AZIMUTH_MIN_VALUE = -500'000'000;

constexpr int32_t ELEVATION_MAX_VALUE = 1'000'000'000;
constexpr int32_t ELEVATION_MIN_VALUE = 100'000;

ServoCommunicator::ServoCommunicator(BS::thread_pool &threadPool, StreamingConfig &config) : socket_(socket(AF_INET, SOCK_DGRAM, 0)) {

    if (socket_ < 0) {
        LOG_ERROR("socket creation failed");
        return;
    }

    memset(&myAddr_, 0, sizeof(myAddr_));
    myAddr_.sin_family = AF_INET;
    myAddr_.sin_addr.s_addr = INADDR_ANY;
    myAddr_.sin_port = htons(IP_CONFIG_SERVO_PORT);

    if (bind(socket_, (sockaddr *) &myAddr_, sizeof(myAddr_)) < 0) {
        LOG_ERROR("bind socket failed");
    }

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = RESPONSE_TIMEOUT_US;

    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        LOG_ERROR("setsockopt failed");
    }

    memset(&destAddr_, 0, sizeof(destAddr_));
    destAddr_.sin_family = AF_INET;
    destAddr_.sin_addr.s_addr = inet_addr(IpToString(config.jetson_ip).c_str());
    destAddr_.sin_port = htons(IP_CONFIG_SERVO_PORT);

    setMode(threadPool);
}

void ServoCommunicator::resetErrors(BS::thread_pool &threadPool) {
    if (!checkReadiness()) {
        return;
    }

    threadPool.push_task([this]() {
        std::vector<unsigned char> const buffer = {IDENTIFIER_1, IDENTIFIER_2,
                                                         Operation::WRITE,
                                                         MessageGroup::ENABLE_AZIMUTH, MessageElement::ENABLE,
                                                         0x08, 0x00, 0x00, 0x00,
                                                         Operation::WRITE,
                                                         MessageGroup::ENABLE_ELEVATION, MessageElement::ENABLE,
                                                         0x08, 0x00, 0x00, 0x00};

        while (true) {
            sendMessage(buffer);

            if (waitForResponse({5, 9})) {
                break;
            }
        }
    });
}


void ServoCommunicator::enableServos(bool enable, BS::thread_pool &threadPool) {
    if (!checkReadiness()) {
        return;
    }

    threadPool.push_task([this, enable]() {
        unsigned char const en = enable ? 0x01 : 0x00;

        std::vector<unsigned char> const enableBuffer = {IDENTIFIER_1, IDENTIFIER_2,
                                                         Operation::WRITE,
                                                         MessageGroup::ENABLE_AZIMUTH, MessageElement::ENABLE,
                                                         en, 0x00, 0x00, 0x00,
                                                         Operation::WRITE,
                                                         MessageGroup::ENABLE_ELEVATION, MessageElement::ENABLE,
                                                         en, 0x00, 0x00, 0x00};

        while (true) {
            sendMessage(enableBuffer);

            if (waitForResponse({5, 9})) {
                break;
            }
        }

        servosEnabled_ = enable;
        if (enable) {
            LOG_ERROR("Servos enabled!");
        } else {
            LOG_ERROR("Servos disabled!");
        }
    });
}

void ServoCommunicator::setPoseAndSpeed(XrQuaternionf quatPose, int32_t speed, BS::thread_pool &threadPool) {
    if (!checkReadiness()) {
        return;
    }

    threadPool.push_task([this, quatPose, speed]() {
        auto azimuthElevation = quaternionToAzimuthElevation(quatPose);

        auto azimuth_max_side = int32_t((int64_t(AZIMUTH_MAX_VALUE) - AZIMUTH_MIN_VALUE) / 2);
        auto azimuth_center = AZIMUTH_MAX_VALUE - azimuth_max_side;

        auto elevation_max_side = int32_t((int64_t(ELEVATION_MAX_VALUE) - ELEVATION_MIN_VALUE) / 2);
        auto elevation_center = ELEVATION_MAX_VALUE - elevation_max_side;

        auto azimuth = int32_t(((azimuthElevation.azimuth * 2.0F) / M_PI) * azimuth_max_side + azimuth_center);
        auto elevation = int32_t(((-azimuthElevation.elevation * 2.0F) / M_PI) * elevation_max_side + elevation_center);

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
                                                        azAngleBytes[0], azAngleBytes[1], azAngleBytes[2], azAngleBytes[3],
                                                        azRevolBytes[0], azRevolBytes[1], azRevolBytes[2], azRevolBytes[3],
                                                        Operation::WRITE_CONTINUOS,
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

        while (true) {
            sendMessage(buffer);

            if (waitForResponse({5, 10})) {
                auto newStamp = std::chrono::high_resolution_clock::now();
                LOG_ERROR("ServoCommunication FPS: %f ",
                          1e6f/std::chrono::duration_cast<std::chrono::microseconds>(newStamp - timestamp_).count());
                timestamp_ = newStamp;

                break;
            }
        }
    });
}

void ServoCommunicator::sendOdinControlPacket(float linSpeedX, float linSpeedY, float angSpeed, BS::thread_pool &threadPool) {
    threadPool.push_task([this, linSpeedX, linSpeedY, angSpeed]() {
        float x = linSpeedX * 0.25f;
        float y = -linSpeedY * 0.25f;
        float a = -angSpeed * 0.25f;

        auto linSpeedXBytes = serializeLEFloat(x);
        auto linSpeedYBytes = serializeLEFloat(y);
        auto angSpeedBytes = serializeLEFloat(a);

        std::vector<unsigned char> const buffer = {0x23,
                                                   0x00, 0x01,
                                                   linSpeedXBytes[0], linSpeedXBytes[1], linSpeedXBytes[2], linSpeedXBytes[3],
                                                   linSpeedYBytes[0], linSpeedYBytes[1], linSpeedYBytes[2], linSpeedYBytes[3],
                                                   angSpeedBytes[0], angSpeedBytes[1], angSpeedBytes[2], angSpeedBytes[3],
                                                   0x00, 0x00
        };

        LOG_INFO("Input speedX: %f, serialized: %x, %x, %x, %x", x, linSpeedXBytes[0], linSpeedXBytes[1], linSpeedXBytes[2], linSpeedXBytes[3]);
        LOG_INFO("Input speedY: %f, serialized: %x, %x, %x, %x", y, linSpeedYBytes[0], linSpeedYBytes[1], linSpeedYBytes[2], linSpeedYBytes[3]);

        sendMessage(buffer);
        isReady_ = true;
    });
}

bool ServoCommunicator::checkReadiness() const {
    if (!isInitialized()) {
        //LOG_ERROR("ServoCommunicator is not yet initialized!");
        return false;
    }

    if (!isReady()) {
        //LOG_ERROR("ServoCommunicator is not currently ready to communicate!");
        return false;
    }

    return true;
}

void ServoCommunicator::setMode(BS::thread_pool &threadPool) {
    threadPool.push_task([this]() {
        std::vector<unsigned char> const modeBuffer = {IDENTIFIER_1, IDENTIFIER_2,
                                                       Operation::WRITE,
                                                       MessageGroup::AZIMUTH, MessageElement::MODE,
                                                       0x01, 0x00, 0x00, 0x00,
                                                       Operation::WRITE,
                                                       MessageGroup::ELEVATION, MessageElement::MODE,
                                                       0x01, 0x00, 0x00, 0x00};

        while (true) {
            sendMessage(modeBuffer);

            if (waitForResponse({5, 9})) {
                break;
            }
        }

        isInitialized_ = true;
        isReady_ = true;

        LOG_ERROR("ServoCommunicator correctly initialized, mode set to position on both axes");
    });
}

void ServoCommunicator::sendMessage(const std::vector<unsigned char> &message) {
    if (sendto(socket_, message.data(), message.size(), 0, (sockaddr *) &destAddr_, sizeof(destAddr_)) < 0) {
        //LOG_ERROR("failed to send message");
    }
    isReady_ = false;
}

bool ServoCommunicator::waitForResponse(const std::vector<uint32_t> &statusBytes) {
    std::array<char, 1024> buffer{};
    struct sockaddr_in src_addr{};
    socklen_t addrlen = sizeof(src_addr);

    size_t nbytes = recvfrom(socket_, buffer.data(), sizeof(buffer), 0, (sockaddr *) &src_addr, &addrlen);
    if (nbytes <= 0) {
        if (errno == EWOULDBLOCK) {
            LOG_ERROR("Timeout reached. No data received.");
        } else {
            LOG_ERROR("recvfrom failed");
        }
        return false;
    }

    std::string response;
    if (nbytes < 100) {
        for (int i = 0; i < nbytes; i++) {
            response += buffer[i];
        }
    }

    isReady_ = true;
    if (response.length() == 0) {
        //LOG_ERROR("No response received! Check if the servo is connected properly");
    } else if (response.length() < RESPONSE_MIN_BYTES) {
        LOG_ERROR("Malformed packet received!");
    } else {
        std::cout << "Received response: " << response.c_str() << std::endl;
        bool isOk = true;
        for (uint32_t const status: statusBytes) {
            isOk &= response.c_str()[status] == '\0'; // Xth byte should be 0
        }
        return isOk;
    }

    return false;
}

ServoCommunicator::AzimuthElevation ServoCommunicator::quaternionToAzimuthElevation(XrQuaternionf q) {
    double azimuth = 0;
    double elevation = 0;
    double test = q.x * q.y + q.z * q.w;
    if (test > 0.499F) { // Singularity at north pole
        azimuth = 2 * std::atan2(q.x, q.w);
        elevation = 0;
        LOG_INFO("quat x: %2.2f, y: %2.2f, z: %2.2f, w: %2.2f; Azimuth: %2.2f, Elevation: %2.2f", q.x, q.y, q.z, q.w, azimuth, elevation);
        return AzimuthElevation{azimuth, elevation};
    }

    if (test < -0.499F) { // Singularity at south pole
        azimuth = -2 * std::atan2(q.x, q.w);
        elevation = 0;
        LOG_INFO("quat x: %2.2f, y: %2.2f, z: %2.2f, w: %2.2f; Azimuth: %2.2f, Elevation: %2.2f", q.x, q.y, q.z, q.w, azimuth, elevation);
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


    LOG_INFO("quat x: %2.2f, y: %2.2f, z: %2.2f, w: %2.2f; Azimuth: %2.2f, Elevation: %2.2f", q.x, q.y, q.z, q.w, azimuth, elevation);
    return AzimuthElevation{azimuth, elevation + 0.5f};
}
