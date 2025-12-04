// host_vulkan.h
#pragma once
#include <vulkan/vulkan.h>
#include <stdint.h>

typedef struct {
    VkInstance instance;
} HVkInstance;

typedef struct {
    VkDevice device;
} HVkDevice;

int hostvk_init();

uint64_t hostvk_create_instance();
uint32_t hostvk_enum_physical_devices(uint64_t inst_handle);
uint64_t hostvk_create_device(uint64_t inst_handle);
HVkInstance* hostvk_get_instance(uint64_t h);
HVkDevice*   hostvk_get_device(uint64_t h);
