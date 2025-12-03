cmake_minimum_required(VERSION 3.10)
project(virtio_vulkan_icd C)

set(CMAKE_C_STANDARD 99)

# 不要链接 Vulkan loader！
# find_package(Vulkan REQUIRED)

add_library(vulkan_virtio_icd SHARED virtio_icd.c)

# ICD 输出名字必须是 libvulkan_xxx.so
set_target_properties(vulkan_virtio_icd PROPERTIES
    OUTPUT_NAME "vulkan_virtio"
)

