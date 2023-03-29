#pragma once

#include <openxr/openxr_reflection.h>

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
};