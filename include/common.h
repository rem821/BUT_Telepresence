#pragma once

#include <openxr/openxr_reflection.h>
#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

// <!-- IP CONFIGURATION SECTION --!>
constexpr std::string_view IP_CONFIG_JETSON_ADDR = "jetsontelepresence.zapto.org";
constexpr std::string_view IP_CONFIG_HEADSET_ADDR = "vrtelepresence.zapto.org";
constexpr int IP_CONFIG_REST_API_PORT = 32281;
constexpr int IP_CONFIG_SERVO_PORT = 32115;
constexpr int IP_CONFIG_LEFT_CAMERA_PORT = 8554;
constexpr int IP_CONFIG_RIGHT_CAMERA_PORT = 8556;


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
    JPEG, VP8, VP9, H264, H265
};

enum VideoMode {
    STEREO, MONO
};

struct CameraStats {
    double prevTimestamp, currTimestamp;
    double fps;
    uint64_t nvvidconv, jpegenc, rtpjpegpay, udpstream, rtpjpegdepay, jpegdec, queue;
    uint64_t rtpjpegpayTimestamp, udpsrcTimestamp, rtpjpegdepayTimestamp, jpegdecTimestamp, queueTimestamp;
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
    std::string ip{"1.2.3.4"};
    int portLeft{IP_CONFIG_LEFT_CAMERA_PORT};
    int portRight{IP_CONFIG_RIGHT_CAMERA_PORT};
    Codec codec{JPEG}; //TODO: Implement different codecs
    int encodingQuality{25};
    int bitrate{4000}; //TODO: Implement rate control
    int horizontalResolution{1920}, verticalResolution{
            1080}; //TODO: Restrict to specific supported resolutions
    VideoMode videoMode{STEREO};
    int fps{60};
};

struct SystemInfo {
    std::string openXrRuntime;
    std::string openXrSystem;
    const unsigned char *openGlVersion;
    const unsigned char *openGlVendor;
    const unsigned char *openGlRenderer;
};

struct AppState {
    CamPair cameraStreamingStates{};
    StreamingConfig streamingConfig{};
    float appFrameRate{0.0f};
    long long appFrameTime{0};
    SystemInfo systemInfo;
};