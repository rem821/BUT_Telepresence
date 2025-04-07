#pragma once

#include <openxr/openxr_reflection.h>
#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <vector>

// <!-- IP CONFIGURATION SECTION --!>
constexpr uint8_t IP_CONFIG_JETSON_ADDR[4] = {192,168,1,188};
constexpr uint8_t IP_CONFIG_HEADSET_ADDR[4] = {192,168,1,49};
constexpr int IP_CONFIG_REST_API_PORT = 32281;
constexpr int IP_CONFIG_SERVO_PORT = 32115;
constexpr int IP_CONFIG_LEFT_CAMERA_PORT = 8554;
constexpr int IP_CONFIG_RIGHT_CAMERA_PORT = 8556;


inline std::string resolveIPv4(const std::string& hostname) {
    return hostname;
}
/*
inline std::string resolveIPv4(const std::string& hostname) {
    struct addrinfo hints, *res, *p;
    int status;
    char ipstr[INET_ADDRSTRLEN];  // To hold the IPv4 address

    // Clear the hints struct
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;     // AF_INET for IPv4 only
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets

    // Resolve the domain name to an IPv4 address
    if ((status = getaddrinfo(hostname.c_str(), NULL, &hints, &res)) != 0) {
        std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
        return "";
    }

    // Loop through the results and find the first IPv4 address
    for (p = res; p != NULL; p = p->ai_next) {
        if (p->ai_family == AF_INET) {  // IPv4 address
            struct sockaddr_in* ipv4 = (struct sockaddr_in*)p->ai_addr;
            // Convert the IP to a string and return it
            inet_ntop(p->ai_family, &(ipv4->sin_addr), ipstr, sizeof ipstr);
            freeaddrinfo(res);  // Free the allocated memory
            return std::string(ipstr);  // Return the IPv4 address as a string
        }
    }

    // If no IPv4 address was found, return an empty string
    freeaddrinfo(res);  // Free the allocated memory
    return "";
}
*/
// <!-- IP CONFIGURATION SECTION --!>

inline std::string Fmt(const char *fmt, ...) {
    va_list vl;
    va_start(vl, fmt);
    int size = std::vsnprintf(nullptr, 0, fmt, vl);
    va_end(vl);

    if (size != -1) {
        std::unique_ptr<char[]> buffer(new char[size + 1]);

        va_start(vl, fmt);
        size = std::vsnprintf(buffer.get(), size + 1, fmt, vl);
        va_end(vl);
        if (size != -1) {
            return std::string(buffer.get(), size);
        }
    }

    throw std::runtime_error("Unexpected vsnprintf failure");
}

inline std::string GetXrVersionString(XrVersion ver) {
    return Fmt("%d.%d.%d", XR_VERSION_MAJOR(ver), XR_VERSION_MINOR(ver), XR_VERSION_PATCH(ver));
}

inline bool EqualsIgnoreCase(const std::string &s1, const std::string &s2,
                             const std::locale &loc = std::locale()) {
    const std::ctype<char> &ctype = std::use_facet<std::ctype<char>>(loc);
    const auto compareCharLower = [&](char c1, char c2) {
        return ctype.tolower(c1) == ctype.tolower(c2);
    };
    return s1.size() == s2.size() && std::equal(s1.begin(), s1.end(), s2.begin(), compareCharLower);
}

template<typename T, size_t Size>
constexpr size_t ArraySize(const T (&/* unused */)[Size]) noexcept {
    return Size;
}

namespace Side {
    const int LEFT = 0;
    const int RIGHT = 1;
    const int COUNT = 2;
}

struct UserState {
    XrPosef hmdPose;
    XrPosef controllerPose[Side::COUNT];
    XrVector2f thumbstickPose[Side::COUNT];
    bool thumbstickPressed[Side::COUNT];
    bool thumbstickTouched[Side::COUNT];
    float squeezeValue[Side::COUNT];
    float triggerValue[Side::COUNT];
    bool triggerTouched[Side::COUNT];
    bool aPressed;
    bool aTouched;
    bool bPressed;
    bool bTouched;
    bool xPressed;
    bool xTouched;
    bool yPressed;
    bool yTouched;
};

enum Codec {
    JPEG, VP8, VP9, H264, H265, Count
};

inline std::string CodecToString(Codec codec) {
    switch(codec) {
        case JPEG:
            return "JPEG";
            break;
        case VP8:
            return "VP8";
            break;
        case VP9:
            return "VP9";
            break;
        case H264:
            return "H264";
            break;
        case H265:
            return "H265";
            break;
        default:
            return "Unknown";
            break;
    }
}

enum VideoMode {
    STEREO, MONO, CNT
};

inline std::string VideoModeToString(VideoMode mode) {
    switch(mode) {
        case STEREO:
            return "STEREO";
            break;
        case MONO:
            return "MONO";
            break;
        default:
            return "Unknown";
            break;
    }
}

inline std::string IpToString(const std::vector<uint8_t> ip) {
    std::ostringstream oss;
    oss << static_cast<int>(ip[0]) << "."
        << static_cast<int>(ip[1]) << "."
        << static_cast<int>(ip[2]) << "."
        << static_cast<int>(ip[3]);
    return oss.str();
}

inline std::vector<uint8_t> StringToIp(const std::string& ipStr) {
    std::vector<uint8_t> ip(4);
    std::istringstream iss(ipStr);
    std::string segment;
    int i = 0;

    while (std::getline(iss, segment, '.')) {
        if (i >= 4) {
            throw std::invalid_argument("Invalid IP address format: too many segments");
        }

        int value = std::stoi(segment);
        if (value < 0 || value > 255) {
            throw std::out_of_range("IP address segment out of range: " + segment);
        }

        ip[i++] = static_cast<uint8_t>(value);
    }

    if (i != 4) {
        throw std::invalid_argument("Invalid IP address format: not enough segments");
    }

    return ip;
}

inline std::vector<uint8_t> GetLocalIPAddr() {
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    sockaddr_in loopback{};

    if(sock == -1) {
        std::cerr << "Error creating socket" << std::endl;
        return {};
    }

    std::memset(&loopback, 0, sizeof(loopback));
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = 1337;
    loopback.sin_port = htons(9);

    if(connect(sock, reinterpret_cast<sockaddr*>(&loopback), sizeof(loopback)) == -1) {
        close(sock);
        std::cerr << "Error connecting while getting local IP" << std::endl;
        return {};
    }

    socklen_t addrlen = sizeof(loopback);
    if(getsockname(sock, reinterpret_cast<sockaddr*>(&loopback), &addrlen) == -1) {
        close(sock);
        std::cerr << "Error getting local IP" << std::endl;
        return {};
    }

    auto ip = std::string(inet_ntoa(loopback.sin_addr));
    std::cout << "Local IP: " << ip << std::endl;
    close(sock);
    auto ipVector = StringToIp(ip);
    return ipVector;
}

inline const char * const BoolToString(bool b)
{
    return b ? "true" : "false";
}

struct CameraStats {
    double prevTimestamp, currTimestamp;
    double fps;
    uint64_t vidConv, enc, rtpPay, udpStream, rtpDepay, dec, queue;
    uint64_t rtpPayTimestamp, udpSrcTimestamp, rtpDepayTimestamp, decTimestamp, queueTimestamp;
    uint64_t totalLatency;
    uint64_t frameId;
};

struct CameraFrame {
    CameraStats *stats;
    unsigned long memorySize = 1920 * 1080 * 3; // Size of single Full HD RGB frame
    void *dataHandle;
};

using CamPair = std::pair<CameraFrame, CameraFrame>;

struct StreamingConfig {
    std::vector<uint8_t> headset_ip;
    std::vector<uint8_t> jetson_ip;
    int portLeft{IP_CONFIG_LEFT_CAMERA_PORT};
    int portRight{IP_CONFIG_RIGHT_CAMERA_PORT};
    Codec codec{H264};
    int encodingQuality{60};
    int bitrate{4000};
    int horizontalResolution{1920}, verticalResolution{1080};
    VideoMode videoMode{STEREO};
    int fps{60};

    StreamingConfig()
    {
        std::vector<uint8_t> headsetIPVector(std::begin(IP_CONFIG_HEADSET_ADDR), std::end(IP_CONFIG_HEADSET_ADDR));
        headset_ip = headsetIPVector;

        std::vector<uint8_t> jetsonIPVector(std::begin(IP_CONFIG_JETSON_ADDR), std::end(IP_CONFIG_JETSON_ADDR));
        jetson_ip = jetsonIPVector;
    }
};

struct SystemInfo {
    std::string openXrRuntime;
    std::string openXrSystem;
    const unsigned char *openGlVersion;
    const unsigned char *openGlVendor;
    const unsigned char *openGlRenderer;
};

struct GUIControl {
    bool focusMoveUp, focusMoveDown, focusMoveLeft, focusMoveRight;
    int focusedElement = 0, focusedSegment = 0; // Element and its segment that currently has focus
    bool changesEnqueued = false; // If this flag si set, do not modify this struct until GUI layer sets it to false
    int cooldown = 0; // Number of frames gui can't be controlled (set in GUI layer, decreased every frame
};

struct AppState {
    CamPair cameraStreamingStates{};
    StreamingConfig streamingConfig{};
    float appFrameRate{0.0f};
    long long appFrameTime{0};
    SystemInfo systemInfo;
    GUIControl guiControl;
    bool robotControlEnabled = true;
};