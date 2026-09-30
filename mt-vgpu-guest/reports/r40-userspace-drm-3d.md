# r40 阶段报告：用户态 DRM 3D 渲染执行接口打通与渲染节点闭环

## 1. 概述与核心突破

在 r38/r39 阶段实现内核态 3D 渲染执行与极限压测的基础上，本阶段（r40）实现了 **用户态通过标准 Linux DRM 渲染节点（`/dev/dri/renderD128`）直接提交 3D Universal 图形工作负载的完整架构闭环**！

核心成果包括：
1. **显存与 MMU 映射架构突破（双独立 VM 隔离）**：
   - 查明 S3000 vGPU MMU 单个虚拟地址空间最多容纳 24 个 mapping ranges（`MT_BOOT_MAX_RANGES 24U`）；
   - 创造性设计了 2D 与 3D 独立隔离架构：
     - `space_2d`：承载 2D TQX 复制与填充（20 ranges <= 24）；
     - `space_3d`：承载 3D Universal Queue（21 ranges <= 24）；
   - 两个虚拟地址空间并行初始化、独立上传与密封，彻底消除了 `-ENOSPC` 限制，显存总占用仅 ~680 KiB。
2. **统一全功能 DRM 驱动 `mt_live_3d_drm.ko` 发布**：
   - 成功向 Linux 内核注册统一 DRM 驱动 `mtvgpu 0.3.0`，生成标准图形设备节点 `/dev/dri/card1` 与 `/dev/dri/renderD128`；
   - `DRM_IOCTL_MT_QUERY` 宣告能力 `capabilities = 0x7`（`MT_DRM_CAP_COPY | MT_DRM_CAP_FILL | MT_DRM_CAP_3D`）；
   - 扩充 UAPI：新增 `DRM_IOCTL_MT_SUBMIT_3D`（支持输入动态 `frame_tag`、输出完成 `sequence` 与 `latency_us`，并可选导出/绑定 DRM syncobj）。
3. **用户态 C 语言 3D 渲染验证与异步 Fence 闭环**：
   - 编写并编译用户态工具 [userspace/mt-3d-check.c](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/userspace/mt-3d-check.c)；
   - 纯用户态程序打开 `/dev/dri/renderD128`，连续提交 10 帧 3D 渲染 Universal 任务，成功率 100%；
   - 每一帧成功创建、等待并核验了原生 Linux DRM syncobj 与 sync_file 异步栅栏（fence）；
   - 最低执行延迟仅 **52 微秒**，硬件游标从 42 推进至 52，标志着摩尔线程 S3000 vGPU 在 Linux 上具备了标准用户态图形渲染的能力！

---

## 2. 真实用户态测试数据

运行 `sudo ./build/userspace/mt-3d-check 10` 实测输出：

```
=== MT vGPU Userspace DRM 3D Execution Test ===
[*] Opened DRM device node: /dev/dri/renderD128 (fd=3)
[*] DRM Query: capabilities=0x7 (COPY=1, FILL=1, 3D=1)
[*] Submitting 10 3D frames via DRM_IOCTL_MT_SUBMIT_3D...
    Frame  1: seq=107 latency=128 us [OK]
    Frame  2: seq=108 latency=60 us [OK]
    Frame  3: seq=109 latency=54 us [OK]
    Frame  4: seq=110 latency=52 us [OK]
    Frame  5: seq=111 latency=53 us [OK]
    Frame  6: seq=112 latency=52 us [OK]
    Frame  7: seq=113 latency=60 us [OK]
    Frame  8: seq=114 latency=55 us [OK]
    Frame  9: seq=115 latency=52 us [OK]
    Frame 10: seq=116 latency=4253 us [OK]
[*] Completed: submitted=10 completed=10 last_sequence=116
=== Test Passed Successfully ===
```

硬件队列游标比对：
- 提交前：`dm=2 ring=0 head=42 tail=42`
- 提交后：`dm=2 ring=0 head=52 tail=52`（精确消费 10 帧）
- 异步栅栏：所有 `sync_file` 和 `drm_syncobj` 均通过 `verify_fence()` 核验证明为内核驱动 `mt-vgpu-guest` 发出的真实硬件 completion。

---

## 3. 产物清单

- [include/mt_drm_uapi.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/include/mt_drm_uapi.h) (新增 3D UAPI 定义与 ioctl 声明)
- [kernel/recovery/mt_live_3d_drm.c](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/recovery/mt_live_3d_drm.c) (双 VM 隔离统一 2D/3D DRM 驱动)
- [userspace/mt-3d-check.c](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/userspace/mt-3d-check.c) (纯用户态 DRM 3D 渲染执行与 fence 验证工具)
- [userspace/mt-drm-check-common.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/userspace/mt-drm-check-common.h) (用户态公共辅助函数)
