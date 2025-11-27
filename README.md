# vulkan-virtio-pass-through
基于 Virtio-PCI 协议和 Vulkan ICD（Installable Client Driver）规范，构建一套轻量、兼容标准的 Vulkan 指令透传框架，实现单台宿主机的物理 GPU 资源被多台 Linux 虚拟机共享，且无需依赖显卡厂商的 SR-IOV 硬件特性。
