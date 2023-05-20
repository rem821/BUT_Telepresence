cmake_minimum_required(VERSION 3.10)
project(but_telepresence)
set(CMAKE_CXX_STANDARD 17)

add_definitions(-DXR_USE_PLATFORM_ANDROID)
add_definitions(-DXR_USE_GRAPHICS_API_VULKAN)
add_definitions(-Werror)
add_definitions(-DNDEBUG)

set(GSTREAMER_ROOT ${GSTREAMER_SDK_DIR}/${ANDROID_ABI})
set(ENV{GST_PLUGIN_PATH} "${GSTREAMER_ROOT}/lib/gstreamer-1.0")
set(ENV{GST_REGISTRY} "${GSTREAMER_ROOT}/registry.bin")

find_library(ANDROID_LIBRARY NAMES android)
find_library(ANDROID_LOG_LIBRARY NAMES log)
find_package(Vulkan REQUIRED)

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

include_directories(
        ${ANDROID_NATIVE_APP_GLUE}
        ${Vulkan_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/3rdParty/OpenXR-SDK/include
        ${GSTREAMER_ROOT}/include/gstreamer-1.0
        ${GSTREAMER_ROOT}/include/glib-2.0
        ${GSTREAMER_ROOT}/lib/glib-2.0/include
        ${OVR_OPENXR_MOBILE_SDK_DIR}/3rdParty/khronos/openxr/OpenXR-SDK/include
        ${ANDROID_NDK}/sources/third_party/shaderc/include
)

link_directories(
        ${GSTREAMER_ROOT}/lib
        ${GSTREAMER_ROOT}/lib/gstreamer-1.0
)

add_library(
        ${PROJECT_NAME} MODULE

        src/main.cpp
        src/platform_plugin_android.cpp
        src/program.cpp
        src/gstreamer_player.cpp
        src/udp_socket.cpp
        src/servo_communicator.cpp

        src/vulkan/VulkanGraphicsContext.cpp
        src/vulkan/VulkanDevice.cpp
        src/vulkan/VulkanDescriptors.cpp
        src/vulkan/VulkanPipeline.cpp
        src/vulkan/VulkanRenderSystem.cpp
        src/vulkan/VulkanRenderer.cpp
        src/vulkan/VulkanSwapChain.cpp

        $<TARGET_OBJECTS:android_native_app_glue>
)

target_link_libraries(
        ${PROJECT_NAME}
        openxr_loader
        ${ANDROID_LIBRARY}
        ${ANDROID_LOG_LIBRARY}
        ${Vulkan_LIBRARY}
        ${GSTREAMER_ROOT}/lib/libgstreamer_android.so
        ${ANDROID_NDK}/sources/third_party/shaderc/libs/c++_shared/${ANDROID_ABI}/libshaderc.a
        ${PROJECT_SOURCE_DIR}/src/vulkan/validationLayers/${ANDROID_ABI}/libVkLayer_khronos_validation.so
)
