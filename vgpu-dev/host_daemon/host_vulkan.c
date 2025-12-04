#include "host_vulkan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr = NULL;

static HVkInstance instances[128];
static HVkDevice   devices[128];

static uint64_t inst_count = 1;
static uint64_t dev_count  = 1;

#define LOG(...) printf("[hostvk] " __VA_ARGS__)

/* ----------------------------------------------
 * 初始化：加载宿主 Vulkan Loader
 * ---------------------------------------------- */
int hostvk_init()
{
    void* lib = dlopen("libvulkan.so", RTLD_NOW);
    if (!lib) {
        LOG("无法加载 libvulkan.so\n");
        return -1;
    }

    pfnGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");

    if (!pfnGetInstanceProcAddr) {
        LOG("无法找到 vkGetInstanceProcAddr\n");
        return -1;
    }

    LOG("Host Vulkan 已加载\n");
    return 0;
}

/* ----------------------------------------------
 * 创建 Vulkan Instance
 * ---------------------------------------------- */
uint64_t hostvk_create_instance()
{
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "virtio-vulkan",
        .applicationVersion = 1,
        .pEngineName = "virtio",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };

    PFN_vkCreateInstance pfn =
        (PFN_vkCreateInstance)pfnGetInstanceProcAddr(NULL, "vkCreateInstance");

    VkInstance inst;
    if (pfn(&ci, NULL, &inst) != VK_SUCCESS) {
        LOG("vkCreateInstance 失败\n");
        return 0;
    }

    uint64_t h = inst_count++;
    instances[h].instance = inst;

    LOG("hostvk_create_instance: handle=%lu\n", h);
    return h;
}

/* ----------------------------------------------
 * 枚举物理设备
 * ---------------------------------------------- */
uint32_t hostvk_enum_physical_devices(uint64_t inst_handle)
{
    HVkInstance* hi = &instances[inst_handle];

    PFN_vkEnumeratePhysicalDevices pfnEnum =
        (PFN_vkEnumeratePhysicalDevices)
        pfnGetInstanceProcAddr(hi->instance, "vkEnumeratePhysicalDevices");

    uint32_t count = 0;
    pfnEnum(hi->instance, &count, NULL);

    LOG("hostvk_enum_phys: inst=%lu count=%u\n", inst_handle, count);
    return count;
}

/* ----------------------------------------------
 * 创建 Device（选第 0 个物理设备）
 * ---------------------------------------------- */
uint64_t hostvk_create_device(uint64_t inst_handle)
{
    HVkInstance* hi = &instances[inst_handle];

    PFN_vkEnumeratePhysicalDevices pfnEnum =
        (PFN_vkEnumeratePhysicalDevices)
        pfnGetInstanceProcAddr(hi->instance, "vkEnumeratePhysicalDevices");

    uint32_t count = 0;
    pfnEnum(hi->instance, &count, NULL);

    if (count == 0) {
        LOG("没有找到物理设备\n");
        return 0;
    }

    VkPhysicalDevice devs[8];
    pfnEnum(hi->instance, &count, devs);

    VkPhysicalDevice phys = devs[0]; // 固定选择第一个

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };

    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
    };

    PFN_vkCreateDevice pfnCreateDev =
        (PFN_vkCreateDevice)
        pfnGetInstanceProcAddr(hi->instance, "vkCreateDevice");

    VkDevice dev;
    if (pfnCreateDev(phys, &dci, NULL, &dev) != VK_SUCCESS) {
        LOG("vkCreateDevice 失败\n");
        return 0;
    }

    uint64_t h = dev_count++;
    devices[h].device = dev;

    LOG("hostvk_create_device: handle=%lu\n", h);
    return h;
}

HVkInstance* hostvk_get_instance(uint64_t h) { return &instances[h]; }
HVkDevice*   hostvk_get_device(uint64_t h) { return &devices[h]; }
