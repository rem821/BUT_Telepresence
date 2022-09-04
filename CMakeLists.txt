cmake_minimum_required(VERSION 3.10)

add_definitions(-DXR_USE_PLATFORM_ANDROID)

find_library(ANDROID_LIBRARY NAMES android)
find_library(ANDROID_LOG_LIBRARY NAMES log)

find_path(ANDROID_NATIVE_APP_GLUE android_native_app_glue.h PATHS ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(android_native_app_glue OBJECT "${ANDROID_NATIVE_APP_GLUE}/android_native_app_glue.c")

add_library(
        oculus_openxr_poc MODULE
        main.cpp
        $<TARGET_OBJECTS:android_native_app_glue>
)

target_link_libraries(
        oculus_openxr_poc
        ${ANDROID_LIBRARY}
        ${ANDROID_LOG_LIBRARY}
)

target_include_directories(
        oculus_openxr_poc
        PRIVATE ${ANDROID_NATIVE_APP_GLUE}
)
