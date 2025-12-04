// vgpu_daemon.c 
// 简单的 vGPU host 端守护进程，配合 guest 侧 virtio Vulkan ICD 使用。

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../guest_icd/vk_virtio_proto.h"

/* ============================================================
 *                    简单工具函数：完整收发
 * ============================================================ */

static ssize_t read_full(int fd, void *buf, size_t size)
{
    size_t off = 0;
    while (off < size)
    {
        ssize_t n = recv(fd, (char *)buf + off, size - off, 0);
        if (n == 0)
        {
            // 对端关闭
            return 0;
        }
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            perror("[daemon] recv");
            return -1;
        }
        off += (size_t)n;
    }
    return (ssize_t)off;
}

static ssize_t write_full(int fd, const void *buf, size_t size)
{
    size_t off = 0;
    while (off < size)
    {
        ssize_t n = send(fd, (const char *)buf + off, size - off, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            perror("[daemon] send");
            return -1;
        }
        off += (size_t)n;
    }
    return (ssize_t)off;
}

/* ============================================================
 *                         句柄分配（旧逻辑）
 * ============================================================ */

static VkvgpuHandle g_next_handle = 1; // 简单递增 ID，当作 host-side 句柄

/* ============================================================
 *                       服务器 socket
 * ============================================================ */

static int setup_server_socket(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("[daemon] socket");
        exit(1);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VKVGPU_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // 如果之前遗留了 socket 文件，先删掉
    unlink(VKVGPU_SOCKET_PATH);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("[daemon] bind");
        close(fd);
        exit(1);
    }

    if (listen(fd, 4) < 0)
    {
        perror("[daemon] listen");
        close(fd);
        exit(1);
    }

    printf("[daemon] listening on %s\n", VKVGPU_SOCKET_PATH);
    fflush(stdout);
    return fd;
}

/* ============================================================
 *                     命令处理函数（旧版，暂未使用）
 * ============================================================ */

static int handle_enum_physical_devices(int cfd)
{
    printf("[daemon] handle ENUM_PHYSICAL_DEVICES (legacy)\n");

    VkvgpuEnumPhysDevsPayload payload;
    memset(&payload, 0, sizeof(payload));
    payload.count = 1; // 先写死只有 1 个虚拟 GPU

    VkvgpuReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.status = 0;
    reply.payload_size = sizeof(payload);

    if (write_full(cfd, &reply, sizeof(reply)) < 0)
    {
        return -1;
    }
    if (write_full(cfd, &payload, sizeof(payload)) < 0)
    {
        return -1;
    }

    return 0;
}

static int handle_create_instance(int cfd)
{
    printf("[daemon] handle CREATE_INSTANCE (legacy)\n");

    VkvgpuCreateInstanceReplyPayload payload;
    payload.instance_handle = g_next_handle++;

    printf("[daemon] allocate instance handle=%lu\n",
           (unsigned long)payload.instance_handle);

    VkvgpuReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.status = 0;
    reply.payload_size = sizeof(payload);

    if (write_full(cfd, &reply, sizeof(reply)) < 0)
    {
        return -1;
    }
    if (write_full(cfd, &payload, sizeof(payload)) < 0)
    {
        return -1;
    }

    return 0;
}

static int handle_create_device(int cfd, const VkvgpuMessage *msg)
{
    printf("[daemon] handle CREATE_DEVICE (legacy), expect payload_size=%zu, got=%u\n",
           sizeof(VkvgpuCreateDeviceRequestPayload),
           msg->header.payload_size);

    if (msg->header.payload_size != sizeof(VkvgpuCreateDeviceRequestPayload))
    {
        printf("[daemon] bad payload_size for CREATE_DEVICE: %u\n",
               msg->header.payload_size);

        VkvgpuReply reply;
        memset(&reply, 0, sizeof(reply));
        reply.status = -1;
        reply.payload_size = 0;
        (void)write_full(cfd, &reply, sizeof(reply));
        return 0; // 协议错误但连接继续
    }

    // 读取请求 payload
    VkvgpuCreateDeviceRequestPayload req;
    ssize_t n = read_full(cfd, &req, sizeof(req));
    if (n <= 0)
    {
        if (n == 0)
        {
            printf("[daemon] client closed while reading create_device req\n");
        }
        return -1;
    }

    printf("[daemon] create device for instance_handle=%lu\n",
           (unsigned long)req.instance_handle);

    VkvgpuCreateDeviceReplyPayload payload;
    payload.device_handle = g_next_handle++;

    printf("[daemon] allocate device handle=%lu\n",
           (unsigned long)payload.device_handle);

    VkvgpuReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.status = 0;
    reply.payload_size = sizeof(payload);

    if (write_full(cfd, &reply, sizeof(reply)) < 0)
    {
        return -1;
    }
    if (write_full(cfd, &payload, sizeof(payload)) < 0)
    {
        return -1;
    }

    return 0;
}

/* ============================================================
 *                      客户端循环处理（新版）
 * ============================================================ */

extern int hostvk_init();
extern uint64_t hostvk_create_instance();
extern uint32_t hostvk_enum_physical_devices(uint64_t);
extern uint64_t hostvk_create_device(uint64_t);

static void handle_client(int cfd)
{
    while (1)
    {
        VkvgpuMessage msg;
        memset(&msg, 0, sizeof(msg));

        // guest 侧是 send(sizeof(VkvgpuMessage))，这里也按同样大小收；
        // 为安全起见，至少要收够 header。
        ssize_t n = recv(cfd, &msg, sizeof(msg), 0);
        if (n == 0)
        {
            printf("[daemon] client disconnected\n");
            break;
        }
        else if (n < 0)
        {
            perror("[daemon] recv");
            break;
        }

        if (n < (ssize_t)sizeof(VkvgpuHeader))
        {
            printf("[daemon] short header (%zd bytes)\n", n);
            break;
        }

        if (msg.header.magic != VKVGPU_MAGIC)
        {
            printf("[daemon] bad magic: 0x%x\n", msg.header.magic);
            break;
        }

        printf("[daemon] received cmd=%u payload=%u\n",
               msg.header.cmd, msg.header.payload_size);

        int rc = 0;

        switch (msg.header.cmd)
        {
        case VKVGPU_CMD_ENUM_PHYSICAL_DEVICES:
        {
            printf("[daemon] handle ENUM_PHYSICAL_DEVICES (hostvk)\n");

            VkvgpuEnumPhysicalDevicesReplyPayload reply = {0};
            uint64_t inst = msg.pl.enum_phys_devices.instance_handle;

            reply.count = hostvk_enum_physical_devices(inst);

            msg.header.payload_size = (uint32_t)sizeof(reply);
            memcpy(&msg.payload, &reply, sizeof(reply));

            if (write_full(cfd,
                           &msg,
                           sizeof(msg.header) + msg.header.payload_size) < 0)
            {
                rc = -1;
            }
        }
        break;

        case VKVGPU_CMD_CREATE_INSTANCE:
        {
            printf("[daemon] handle CREATE_INSTANCE (hostvk)\n");

            uint64_t h = hostvk_create_instance();

            VkvgpuCreateInstanceReplyPayload reply = {
                .instance_handle = h};

            msg.header.payload_size = (uint32_t)sizeof(reply);
            memcpy(&msg.payload, &reply, sizeof(reply));

            if (write_full(cfd,
                           &msg,
                           sizeof(msg.header) + msg.header.payload_size) < 0)
            {
                rc = -1;
            }
        }
        break;

        case VKVGPU_CMD_CREATE_DEVICE:
        {
            printf("[daemon] handle CREATE_DEVICE (hostvk)\n");

            uint64_t inst = msg.pl.create_device.instance_handle;
            uint64_t devh = hostvk_create_device(inst);

            VkvgpuCreateDeviceReplyPayload reply = {
                .device_handle = devh};

            msg.header.payload_size = (uint32_t)sizeof(reply);
            memcpy(&msg.payload, &reply, sizeof(reply));

            if (write_full(cfd,
                           &msg,
                           sizeof(msg.header) + msg.header.payload_size) < 0)
            {
                rc = -1;
            }
        }
        break;

        default:
        {
            printf("[daemon] unknown cmd=%u, reply status=-1\n", msg.header.cmd);
            VkvgpuReply reply;
            memset(&reply, 0, sizeof(reply));
            reply.status = -1;
            reply.payload_size = 0;
            if (write_full(cfd, &reply, sizeof(reply)) < 0)
            {
                rc = -1;
            }
            break;
        }
        }

        if (rc < 0)
        {
            printf("[daemon] error while handling cmd, closing client\n");
            break;
        }
    }
}

/* ============================================================
 *                             main
 * ============================================================ */

int main(void)
{
    if (hostvk_init() != 0)
    {
        printf("Host Vulkan 初始化失败！\n");
        return -1;
    }

    int sfd = setup_server_socket();

    while (1)
    {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0)
        {
            perror("[daemon] accept");
            continue;
        }

        printf("[daemon] client connected\n");
        handle_client(cfd);
        close(cfd);
    }

    close(sfd);
    return 0;
}
