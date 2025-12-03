#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

int main()
{
    printf("=== vGPU ICD test ===\n");

    // 1. Create Instance
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&ici, NULL, &instance);
    printf("vkCreateInstance -> %d, instance=%p\n", r, (void*)instance);
    if (r != VK_SUCCESS) return 0;

    // 2. Enumerate Physical Devices
    uint32_t gpuCount = 0;
    r = vkEnumeratePhysicalDevices(instance, &gpuCount, NULL);
    printf("vkEnumeratePhysicalDevices (count pass 1) -> %d, count=%u\n", r, gpuCount);

    VkPhysicalDevice phys[8];
    r = vkEnumeratePhysicalDevices(instance, &gpuCount, phys);
    printf("vkEnumeratePhysicalDevices (count pass 2) -> %d, count=%u\n", r, gpuCount);

    if (gpuCount == 0) {
        printf("No GPU found. Exit.\n");
        return 0;
    }

    VkPhysicalDevice pd = phys[0];

    // 3. Create Device
    VkDeviceCreateInfo dci;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    VkDevice device = VK_NULL_HANDLE;
    r = vkCreateDevice(pd, &dci, NULL, &device);
    printf("vkCreateDevice -> %d, device=%p\n", r, (void*)device);

    // 4. Destroy Device & Instance
    if (device) {
        vkDestroyDevice(device, NULL);
        printf("vkDestroyDevice done.\n");
    }

    vkDestroyInstance(instance, NULL);
    printf("vkDestroyInstance done.\n");

    return 0;
}

