cmake_minimum_required(VERSION 3.10)

add_definitions(-DXR_USE_PLATFORM_ANDROID)
add_definitions(-DXR_USE_GRAPHICS_API_OPENGL_ES)

find_library(ANDROID_LIBRARY NAMES android)
find_library(ANDROID_LOG_LIBRARY NAMES log)

find_path(ANDROID_NATIVE_APP_GLUE android_native_app_glue.h PATHS ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(android_native_app_glue OBJECT "${ANDROID_NATIVE_APP_GLUE}/android_native_app_glue.c")

add_library(openxr_loader SHARED IMPORTED)
set_property(
        TARGET
        openxr_loader
        PROPERTY
        IMPORTED_LOCATION
        ${OVR_OPENXR_MOBILE_SDK_DIR}/OpenXR/Libs/Android/${ANDROID_ABI}/${CMAKE_BUILD_TYPE}/libopenxr_loader.so
)

add_library(
        oculus_openxr_poc MODULE

        src/graphics_plugin_opengles.cpp
        src/main.cpp
        src/platform_plugin_android.cpp
        src/program.cpp

        $<TARGET_OBJECTS:android_native_app_glue>
)

target_link_libraries(
        oculus_openxr_poc
        openxr_loader
        ${ANDROID_LIBRARY}
        ${ANDROID_LOG_LIBRARY}
)

target_include_directories(
        oculus_openxr_poc
        PRIVATE ${ANDROID_NATIVE_APP_GLUE}
        PRIVATE ${PROJECT_SOURCE_DIR}/3rdParty/OpenXR-SDK/include
)
