cmake_minimum_required(VERSION 3.10)

add_definitions(-DXR_USE_PLATFORM_ANDROID)
add_definitions(-DXR_USE_GRAPHICS_API_OPENGL_ES)
add_definitions(-Werror)

find_library(ANDROID_LIBRARY NAMES android)
find_library(ANDROID_LOG_LIBRARY NAMES log)
find_library(EGL_LIBRARY NAMES EGL)
find_library(OpenGLES_V3_LIBRARY NAMES GLESv3)

find_path(ANDROID_NATIVE_APP_GLUE android_native_app_glue.h PATHS ${ANDROID_NDK}/sources/android/native_app_glue)
find_path(OpenGLES_V3_INCLUDE_DIR NAMES GLES3/gl3.h)
find_path(OpenGLES_V32_INCLUDE_DIR NAMES GLES3/gl32.h)

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

        src/gpu_window.cpp
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
        ${EGL_LIBRARY}
        ${OpenGLES_V3_LIBRARY}
)

target_include_directories(
        oculus_openxr_poc
        PRIVATE ${ANDROID_NATIVE_APP_GLUE}
        PRIVATE ${OpenGLES_V3_INCLUDE_DIR}
        PRIVATE ${OpenGLES_V32_INCLUDE_DIR}
        PRIVATE ${PROJECT_SOURCE_DIR}/3rdParty/OpenXR-SDK/include
        PRIVATE ${PROJECT_SOURCE_DIR}/3rdParty/boost/include
)
