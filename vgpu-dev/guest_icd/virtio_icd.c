// virtio_icd.c
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <vulkan/vulkan.h>
#include "vk_virtio_proto.h"

#define LOG(fmt, ...) fprintf(stderr, "[virtio-icd] " fmt "\n", ##__VA_ARGS__)

/* ===========================================================
 *            Socket / Protocol Helpers
 * ===========================================================*/

static int g_sock_fd = -1;

static int ensure_connection(void) {
    if (g_sock_fd >= 0) return 0;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[virtio-icd] socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VKVGPU_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[virtio-icd] connect");
        close(fd);
        return -1;
    }

    g_sock_fd = fd;
    LOG("connected to daemon at %s", VKVGPU_SOCKET_PATH);
    return 0;
}

/* 简单命令：无 payload / 无返回 payload，仅检查 status */
static int send_simple_cmd(VkvgpuCommandType cmd) {
    if (ensure_connection() != 0)
        return -1;

    VkvgpuMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.magic        = VKVGPU_MAGIC;
    msg.header.cmd          = cmd;
    msg.header.payload_size = 0;

    ssize_t n = send(g_sock_fd, &msg, sizeof(msg), 0);
    if (n < (ssize_t)sizeof(VkvgpuHeader)) {
        perror("[virtio-icd] send");
        return -1;
    }

    VkvgpuReply reply;
    n = recv(g_sock_fd, &reply, sizeof(reply), 0);
    if (n < (ssize_t)sizeof(reply)) {
        perror("[virtio-icd] recv");
        return -1;
    }

    LOG("daemon replied status=%d", reply.status);
    return reply.status;
}

/* 枚举物理设备：daemon 返回 GPU 个数 payload */
static int send_enum_physdevs(uint32_t *out_count) {
    if (ensure_connection() != 0)
        return -1;

    VkvgpuMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.magic        = VKVGPU_MAGIC;
    msg.header.cmd          = VKVGPU_CMD_ENUM_PHYSICAL_DEVICES;
    msg.header.payload_size = 0;

    ssize_t n = send(g_sock_fd, &msg, sizeof(msg), 0);
    if (n < (ssize_t)sizeof(VkvgpuHeader)) {
        perror("[virtio-icd] send enum");
        return -1;
    }

    VkvgpuReply reply;
    n = recv(g_sock_fd, &reply, sizeof(reply), 0);
    if (n < (ssize_t)sizeof(reply)) {
        perror("[virtio-icd] recv reply");
        return -1;
    }

    if (reply.status != 0) {
        LOG("daemon returned error status=%d", reply.status);
        return -1;
    }

    if (reply.payload_size != sizeof(VkvgpuEnumPhysDevsPayload)) {
        LOG("unexpected payload_size=%u in ENUM_PHYSICAL_DEVICES", reply.payload_size);
        return -1;
    }

    VkvgpuEnumPhysDevsPayload payload;
    n = recv(g_sock_fd, &payload, sizeof(payload), 0);
    if (n < (ssize_t)sizeof(payload)) {
        perror("[virtio-icd] recv payload");
        return -1;
    }

    LOG("daemon says phys dev count = %u", payload.count);
    *out_count = payload.count;
    return 0;
}

/* CREATE_INSTANCE：daemon 返回 host-side instance handle */
static int send_create_instance(VkvgpuHandle *out_handle) {
    if (ensure_connection() != 0)
        return -1;

    VkvgpuMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.magic        = VKVGPU_MAGIC;
    msg.header.cmd          = VKVGPU_CMD_CREATE_INSTANCE;
    msg.header.payload_size = 0;

    ssize_t n = send(g_sock_fd, &msg, sizeof(msg), 0);
    if (n < (ssize_t)sizeof(VkvgpuHeader)) {
        perror("[virtio-icd] send create_instance");
        return -1;
    }

    VkvgpuReply reply;
    n = recv(g_sock_fd, &reply, sizeof(reply), 0);
    if (n < (ssize_t)sizeof(reply)) {
        perror("[virtio-icd] recv reply");
        return -1;
    }

    if (reply.status != 0) {
        LOG("daemon returned error status=%d", reply.status);
        return -1;
    }

    if (reply.payload_size != sizeof(VkvgpuCreateInstanceReplyPayload)) {
        LOG("unexpected payload_size=%u in CREATE_INSTANCE", reply.payload_size);
        return -1;
    }

    VkvgpuCreateInstanceReplyPayload payload;
    n = recv(g_sock_fd, &payload, sizeof(payload), 0);
    if (n < (ssize_t)sizeof(payload)) {
        perror("[virtio-icd] recv payload");
        return -1;
    }

    LOG("daemon gave instance handle=%lu", (unsigned long)payload.instance_handle);
    *out_handle = payload.instance_handle;
    return 0;
}

/* CREATE_DEVICE：发送 instance_handle，返回 device_handle */
static int send_create_device(VkvgpuHandle instance_handle,
                              VkvgpuHandle *out_device_handle)
{
    if (ensure_connection() != 0)
        return -1;

    VkvgpuCreateDeviceRequestPayload req;
    req.instance_handle = instance_handle;

    VkvgpuMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.magic        = VKVGPU_MAGIC;
    msg.header.cmd          = VKVGPU_CMD_CREATE_DEVICE;
    msg.header.payload_size = sizeof(req);

    ssize_t n = send(g_sock_fd, &msg, sizeof(msg), 0);
    if (n < (ssize_t)sizeof(VkvgpuHeader)) {
        perror("[virtio-icd] send create_device header");
        return -1;
    }
    n = send(g_sock_fd, &req, sizeof(req), 0);
    if (n < (ssize_t)sizeof(req)) {
        perror("[virtio-icd] send create_device payload");
        return -1;
    }

    VkvgpuReply reply;
    n = recv(g_sock_fd, &reply, sizeof(reply), 0);
    if (n < (ssize_t)sizeof(reply)) {
        perror("[virtio-icd] recv create_device reply");
        return -1;
    }
    if (reply.status != 0) {
        LOG("daemon returned error status=%d in CREATE_DEVICE", reply.status);
        return -1;
    }
    if (reply.payload_size != sizeof(VkvgpuCreateDeviceReplyPayload)) {
        LOG("unexpected payload_size=%u in CREATE_DEVICE", reply.payload_size);
        return -1;
    }

    VkvgpuCreateDeviceReplyPayload payload;
    n = recv(g_sock_fd, &payload, sizeof(payload), 0);
    if (n < (ssize_t)sizeof(payload)) {
        perror("[virtio-icd] recv create_device payload");
        return -1;
    }

    LOG("daemon gave device handle=%lu", (unsigned long)payload.device_handle);
    *out_device_handle = payload.device_handle;
    return 0;
}

/* ===========================================================
 *           句柄包装：Instance / Device / PhysDev
 * ===========================================================*/

typedef struct VirtioInstance_T {
    VkvgpuHandle host_instance;
} VirtioInstance_T;

typedef struct VirtioDevice_T {
    VkvgpuHandle host_device;
} VirtioDevice_T;

typedef struct VirtioPhysicalDevice_T {
    uint32_t id;
} VirtioPhysicalDevice_T;

/* 先假设只有 1 个物理设备 */
static VirtioPhysicalDevice_T g_phys_dev_data = { .id = 0 };
static VkPhysicalDevice       g_phys_dev_array[1] = {
    (VkPhysicalDevice)&g_phys_dev_data
};

/* ===========================================================
 *                     Vulkan ICD 实现
 * ===========================================================*/

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(
    const char*           pLayerName,
    uint32_t*             pPropertyCount,
    VkExtensionProperties* pProperties)
{
    (void)pLayerName;
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pProperties) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(
    uint32_t*              pPropertyCount,
    VkLayerProperties*     pProperties)
{
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pProperties) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance*                  pInstance)
{
    (void)pCreateInfo;
    (void)pAllocator;

    LOG("vkCreateInstance");

    VkvgpuHandle host_inst = 0;
    if (send_create_instance(&host_inst) != 0) {
        LOG("send_create_instance failed");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VirtioInstance_T* inst = (VirtioInstance_T*)malloc(sizeof(VirtioInstance_T));
    if (!inst) return VK_ERROR_OUT_OF_HOST_MEMORY;
    inst->host_instance = host_inst;

    *pInstance = (VkInstance)inst;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyInstance(
    VkInstance                   instance,
    const VkAllocationCallbacks* pAllocator)
{
    (void)pAllocator;
    LOG("vkDestroyInstance");
    if (!instance) return;
    VirtioInstance_T* inst = (VirtioInstance_T*)instance;
    free(inst);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumeratePhysicalDevices(
    VkInstance        instance,
    uint32_t*         pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices)
{
    (void)instance;
    LOG("vkEnumeratePhysicalDevices");

    uint32_t count_from_daemon = 0;
    if (send_enum_physdevs(&count_from_daemon) != 0) {
        LOG("send_enum_physdevs failed");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!pPhysicalDevices) {
        *pPhysicalDeviceCount = count_from_daemon;
        return VK_SUCCESS;
    }

    uint32_t to_copy = (*pPhysicalDeviceCount < count_from_daemon)
                       ? *pPhysicalDeviceCount : count_from_daemon;
    if (to_copy > 0) {
        pPhysicalDevices[0] = g_phys_dev_array[0];
    }
    *pPhysicalDeviceCount = to_copy;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(
    VkPhysicalDevice             physicalDevice,
    const VkDeviceCreateInfo*    pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice*                    pDevice)
{
    (void)physicalDevice;
    (void)pCreateInfo;
    (void)pAllocator;

    LOG("vkCreateDevice");

    /* 简单起见：从任意实例创建 device，这里不追踪 phys→inst 关系 */
    VkvgpuHandle host_device = 0;
    /* 随便给一个 instance_handle 0，真正实现可以从 instance 里取 */
    if (send_create_device(0, &host_device) != 0) {
        LOG("send_create_device failed");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VirtioDevice_T* dev = (VirtioDevice_T*)malloc(sizeof(VirtioDevice_T));
    if (!dev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    dev->host_device = host_device;

    *pDevice = (VkDevice)dev;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDevice(
    VkDevice                     device,
    const VkAllocationCallbacks* pAllocator)
{
    (void)pAllocator;
    LOG("vkDestroyDevice");
    if (!device) return;
    VirtioDevice_T* dev = (VirtioDevice_T*)device;
    free(dev);
}

/* 这些函数目前用不到，给 stub，避免 loader 报错 */

VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char*      pLayerName,
    uint32_t*        pPropertyCount,
    VkExtensionProperties* pProperties)
{
    (void)physicalDevice;
    (void)pLayerName;
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pProperties) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(
    VkDevice    device,
    const char* pName)
{
    (void)device;
    (void)pName;
    return NULL;
}

/* ===========================================================
 *             vkGetInstanceProcAddr / ICD 入口
 * ===========================================================*/

static PFN_vkVoidFunction resolve_instance_proc(const char* name)
{
    if (!name) return NULL;

    if (strcmp(name, "vkCreateInstance") == 0)
        return (PFN_vkVoidFunction)vkCreateInstance;
    if (strcmp(name, "vkDestroyInstance") == 0)
        return (PFN_vkVoidFunction)vkDestroyInstance;
    if (strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0)
        return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    if (strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
        return (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties;
    if (strcmp(name, "vkEnumeratePhysicalDevices") == 0)
        return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    if (strcmp(name, "vkCreateDevice") == 0)
        return (PFN_vkVoidFunction)vkCreateDevice;
    if (strcmp(name, "vkDestroyDevice") == 0)
        return (PFN_vkVoidFunction)vkDestroyDevice;
    if (strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0)
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    if (strcmp(name, "vkGetDeviceProcAddr") == 0)
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;

    return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(
    VkInstance  instance,
    const char* pName)
{
    (void)instance;
    PFN_vkVoidFunction f = resolve_instance_proc(pName);
    if (f) return f;
    return NULL;
}

/* loader 用的 ICD 入口 */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(
    VkInstance  instance,
    const char* pName)
{
    return vkGetInstanceProcAddr(instance, pName);
}

/* 对于物理设备级函数，目前没实现，直接返回 NULL 即可 */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(
    VkInstance      instance,
    const char*     pName)
{
    (void)instance;
    (void)pName;
    return NULL;
}

