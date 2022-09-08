#pragma once

#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, "oculus_openxr_poc", __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "oculus_openxr_poc", __VA_ARGS__)
