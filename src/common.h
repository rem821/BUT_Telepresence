// Copyright (c) 2017-2023, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <locale>
#include <algorithm>
#include <stdarg.h>
#include <stddef.h>

#include <openxr/openxr_reflection.h>

// Macro to generate stringify functions for OpenXR enumerations based data provided in openxr_reflection.h
// clang-format off
#define ENUM_CASE_STR(name, val) case name: return #name;
#define MAKE_TO_STRING_FUNC(enumType)                  \
    inline const char* to_string(enumType e) {         \
        switch (e) {                                   \
            XR_LIST_ENUM_##enumType(ENUM_CASE_STR)     \
            default: return "Unknown " #enumType;      \
        }                                              \
    }
// clang-format on

MAKE_TO_STRING_FUNC(XrReferenceSpaceType);
MAKE_TO_STRING_FUNC(XrViewConfigurationType);
MAKE_TO_STRING_FUNC(XrEnvironmentBlendMode);
MAKE_TO_STRING_FUNC(XrSessionState);
MAKE_TO_STRING_FUNC(XrResult);
MAKE_TO_STRING_FUNC(XrFormFactor);

inline bool EqualsIgnoreCase(const std::string& s1, const std::string& s2, const std::locale& loc = std::locale()) {
    const std::ctype<char>& ctype = std::use_facet<std::ctype<char>>(loc);
    const auto compareCharLower = [&](char c1, char c2) { return ctype.tolower(c1) == ctype.tolower(c2); };
    return s1.size() == s2.size() && std::equal(s1.begin(), s1.end(), s2.begin(), compareCharLower);
}

inline std::string Fmt(const char* fmt, ...) {
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