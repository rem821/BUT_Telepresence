#pragma once

#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, "but_telepresence", __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, "but_telepresence", __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "but_telepresence", __VA_ARGS__)
