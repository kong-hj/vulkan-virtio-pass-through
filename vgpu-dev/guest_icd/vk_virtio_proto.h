#pragma once

#include <stdint.h>

#define VKVGPU_SOCKET_PATH "/tmp/vgpu.sock"
#define VKVGPU_MAGIC 0x56564b50u  // 随便的 magic

typedef uint64_t VkvgpuHandle;

typedef enum {
    VKVGPU_CMD_PING                = 1,
    VKVGPU_CMD_CREATE_INSTANCE     = 2,
    VKVGPU_CMD_ENUM_PHYSICAL_DEVICES = 3,
    VKVGPU_CMD_CREATE_DEVICE       = 4,
} VkvgpuCommandType;

typedef struct {
    uint32_t magic;
    uint32_t cmd;          // VkvgpuCommandType
    uint32_t payload_size; // payload 大小（字节）
    uint32_t reserved;
} VkvgpuHeader;

typedef struct {
    VkvgpuHeader header;
    // payload 紧跟其后（如果 payload_size > 0）
} VkvgpuMessage;

/* 通用回复头 */
typedef struct {
    int32_t  status;       // 0 = OK
    uint32_t payload_size; // 后面的 payload 大小
} VkvgpuReply;

/* ENUM_PHYSICAL_DEVICES 的返回 payload */
typedef struct {
    uint32_t count;
    uint32_t reserved;
} VkvgpuEnumPhysDevsPayload;

/* CREATE_INSTANCE 的返回 payload：host 侧 instance handle */
typedef struct {
    VkvgpuHandle instance_handle;
} VkvgpuCreateInstanceReplyPayload;

/* CREATE_DEVICE 请求 payload（简单版，只传 instance handle） */
typedef struct {
    VkvgpuHandle instance_handle;
} VkvgpuCreateDeviceRequestPayload;

/* CREATE_DEVICE 返回 payload：host 侧 device handle */
typedef struct {
    VkvgpuHandle device_handle;
} VkvgpuCreateDeviceReplyPayload;

