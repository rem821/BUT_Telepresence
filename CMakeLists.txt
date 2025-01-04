cmake_minimum_required(VERSION 3.10)
project(but_telepresence)
set(CMAKE_CXX_STANDARD 17)

add_definitions(-DXR_USE_PLATFORM_ANDROID)
add_definitions(-DXR_USE_GRAPHICS_API_OPENGL_ES)
add_definitions(-Werror)

set(GSTREAMER_ROOT ${GSTREAMER_SDK_DIR}/${ANDROID_ABI})
set(ENV{GST_PLUGIN_PATH} "${GSTREAMER_ROOT}/lib/gstreamer-1.0")
set(ENV{GST_REGISTRY} "${GSTREAMER_ROOT}/registry.bin")

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

include_directories(
        ${ANDROID_NATIVE_APP_GLUE}
        ${OpenGLES_V3_INCLUDE_DIR}
        ${OpenGLES_V32_INCLUDE_DIR}
        ${PROJECT_SOURCE_DIR}/external/OpenXR-SDK/include
        ${GSTREAMER_ROOT}/include/gstreamer-1.0
        ${GSTREAMER_ROOT}/include/glib-2.0
        ${GSTREAMER_ROOT}/lib/glib-2.0/include
        ${OVR_OPENXR_MOBILE_SDK_DIR}/SampleCommon/Src
        external
        external/imgui
        external/imgui/backends
        external/cpp-httplib
        external/json/include
        external/fmt/include
        include
)

link_directories(
        ${GSTREAMER_ROOT}/lib
        ${GSTREAMER_ROOT}/lib/gstreamer-1.0)

add_library(
        ${PROJECT_NAME} SHARED

        src/main.cpp
        src/program.cpp
        src/render_scene.cpp
        src/gstreamer_player.cpp
        src/udp_socket.cpp
        src/servo_communicator.cpp
        src/pose_server.cpp
        src/rest_client.cpp
        src/render_imgui.cpp
        src/util_openxr.cpp
        src/util_egl.cpp
        src/util_shader.cpp
        src/util_render_target.cpp
        src/render_texplate.cpp
        src/ntp_timer.cpp
        external/imgui/imgui.cpp
        external/imgui/imgui_demo.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/backends/imgui_impl_android.cpp
        external/imgui/backends/imgui_impl_opengl3.cpp

        $<TARGET_OBJECTS:android_native_app_glue>
)

set(GSTREAMER_LIBRARIES "${GSTREAMER_ROOT}/lib/libgstreamer_android.so")

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        IMGUI_IMPL_OPENGL_ES3
)

target_link_libraries(
        ${PROJECT_NAME}
        openxr_loader
        ${ANDROID_LIBRARY}
        ${ANDROID_LOG_LIBRARY}
        ${EGL_LIBRARY}
        ${OpenGLES_V3_LIBRARY}
        ${GSTREAMER_LIBRARIES}
)
