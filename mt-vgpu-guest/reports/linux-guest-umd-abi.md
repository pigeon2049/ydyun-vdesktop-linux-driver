# Linux Guest 用户态 ABI 兼容性审计

日期：2026-09-29。官方 Linux 软件包仅下载、解包到 `/tmp` 并作离线分析；没有安装软件包、加载模块或访问显卡。

## 发现的官方 Linux 用户态

摩尔线程 Ubuntu 22.04 Jammy 软件源当前提供完整 Linux 图形/计算用户态。已下载并校验下列 5.2.0 包：

| 包 | SHA-256 |
| --- | --- |
| `libdrm-mtgpu` | `8a5c07b5789721ba6c20b5128a5f06e71f4dee03d866edc6102df2be58bb40c9` |
| `mthreads-legacy-umd` | `0cb86813a105759cf234a5cde4e26bc33a6f238b7e5d2208b23c5643057fbaeb` |
| `libmthreads-gpucomp` | `1afe00bf9ba6c1b1a4552dcf81598cb798e36bda7811d300e61f72cf3c8c1d4c` |
| `libmthreads-gl` | `fec289b218de0740078fe8dcf517fea18398922383d6169d0994c4e7af91fb75` |
| `libmthreads-vulkan` | `53dad5cb4dc1fddbfc5bac227ec083bacfab61c5068c24714964bfc3ec9a604d` |
| `libmthreads-compute` | `254e61c367e10692cfb228e11ea4e3c3401be38b5ca71f372e6042c6d7436924` |
| `musa-musart-5-2` | `ba3e3a4f52b68602382f81542625f544ed65cc0fe3f9cb64e655b1167ac34a5f` |

解包内容包括 `mtgpu_dri.so`、EGL/GLX/Vulkan 驱动、`libdrm_mtgpu.so`、`libmusa.so.5.2.0` 和 `libmusart.so.5.2.0`。这些是官方 Linux 完整驱动栈组件，不是本地 S2000 vGPU 2.3.0 ZIP 中的 Linux Guest 安装包。官方 [Linux PC 驱动支持列表](https://docs.mthreads.com/driver-linux-pc/driver-linux-pc-doc-online/introduction/)没有列出 S3000；当前 [MT vGPU FAQ](https://docs.mthreads.com/vgpu/version-2.9.2/vgpu-doc-online/faq/)说明 Windows Guest 驱动升级流程，但没有提供 Linux Guest 图形驱动安装流程。不要据此推断其它未公开版本。

## 与当前 Guest 候选的 ABI 不匹配

本地 Guest KMD 是从 `mtgpu-1.0.0` 源码构建的。其 UAPI `MTGPU_IOCTL_VERSION` 为 2，并使用多个独立 ioctl（设备初始化、查询、BO、VM、上下文、作业提交等）。官方 5.2.0 Linux 用户态配套的 KMD 头文件将 `MTGPU_IOCTL_VERSION` 提高到 22，所有操作改走 `DRM_IOCTL_MTGPU_CMD`（DRM command `0x0f`）并以子命令区分 core/query/BO/VM/sync/job/perf/KMS 操作。两套 ioctl 号、结构体和版本协商不同，不能把 5.2 用户态直接放到当前 1.0 Guest KMD 上。

5.2.0 DKMS 包中的 `inc/config_kernel.h` 把 `MUSA_NUM_OS_SUPPORTED` 设为 `1`，因此其默认构建只接受 Native 模式，Guest/多 OS 路径被编译条件关闭。临时把它改为 15 后尝试在本机 `6.12.107+deb13-amd64` 编译，首先遇到 `asm/unaligned.h`、`follow_pfn`、`pci_resize_resource`、platform remove 回调等内核 API 变化。只在 `/tmp` 实验副本中加了编译探针兼容处理，并在缺少 `drm_fbdev_generic.h` 时略过 console fbdev helper；DRM/GEM/VPU C 文件随后编译通过，但 MODPOST 因 `vgpu_fw_guest_disconnect`、`vgpu_vdisplay_vsync_register`、`vgpu_vdisplay_vsync_unregister` 未定义而失败，另有 `_MUSADumpMUSAMMUFaultStatus` 仅声明导出的链接错误。DKMS 包的 Makefile 预期把部分 Guest/VZ 对象编入模块，但发布源码树缺少相应实现文件。由此可确认：把 OS 数改成 15 不足以把该 Server DKMS 包变成可链接的 Linux Guest KMD。此实验不是可加载候选，临时 `os_follow_pfn` stub 也只为继续编译探测，未进入正式源码或系统。

## 对当前适配的影响

本机现在有两条可辨别的路径：

1. `2.3.0 Guest-only KMD`：匹配当前 Windows vGPU 信息页适配工作，r10 离线候选已完成构建和静态验证；Guest DRM 初始化仍未运行态通过。原先认为没有可配套的 Linux UMD，现发现 5.2.0 的 `mthreads-legacy-umd` 另带 PVR 接口用户态，其 Connect ABI 与 KMD 1.0 对得上，成为候选路径。
2. `Linux KMD/UMD 5.2.0 新接口`：有完整图形/计算用户态，但用户态 UAPI 为 22、DKMS 默认关闭 Guest/多 OS，且本机 6.12 内核兼容未解决；不能将新接口用户态直接配给 2.3 KMD。

后续 bridge 级审计已修正对 legacy UM 的判断：205 个调用里有 26 个命令未出现在 2.3 KMD 头文件，另有 25 个长度不同；本地 2.7.1 Native 头文件仍缺 7 个命令，官方 5.2 DKMS Host 头文件虽覆盖全部 205 个 ID，仍有 28 个结构长度差异或未建模结构，而且其默认构建关闭 Guest。Sync 分配方面，2.3 handler 忽略 UMD 的 `ui64MemType`，返回 32 位地址；因 staging buffer 零初始化，UM 按 64 位读取时高位补零，但地址范围和忽略内存类型的语义仍待确认。`RGXKICKTA3D3` 多出的 8 字节在 2.3 handler 可见读取范围之外，可能是被忽略的尾部扩展。当前不把 5.2 legacy UMD 视为已验证的 2.3 Guest KMD 配套。详见 [PVR legacy UMD bridge ABI 审计](legacy-umd-pvr-bridge-abi.md) 和 [Connect ABI 审计](legacy-umd-pvr-connect-abi.md)。UM ELF 已完整导出 Ghidra 伪 C，8410/8411 个本地函数成功；唯一反编译失败函数的汇编也已保留在本机 corpus。候选库仍只在隔离目录解包，未安装或加载。

后续先用反编译的 UM 调用图确认启动和普通 3D 提交会触发哪些 2.3 缺失命令，再追 `ui64MemType` 的实际取值；据此再决定是否需要版本门控的 KMD bridge 兼容层。Guest firmware/info-page、OSID backing、固件启动仍是独立的 KMD 阻塞项。
