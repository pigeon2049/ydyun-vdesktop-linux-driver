# r41: 3D Render Target 显存帧缓冲动态绑定与绘制验证

## 1. 背景与目标

在 r40 达成用户态统一 DRM 渲染节点（`/dev/dri/renderD128`）与 Universal 3D 队列硬件提交闭环后，本阶段（r41）重点突破 **3D 硬件 Render Target（渲染目标）显存表面的动态绑定与 VRAM 显存读回闭环**。

核心目标：
1. **并发锁与生命周期解耦**：彻底剥离提交锁（`submit_lock`）与 GEM 对象槽位锁（`slot_lock`），彻底根除 GEM 句柄释放与 ioctl 异常时的重入死锁隐患；
2. **GPU VM 3D 虚拟空间多重映射**：在不超过硬件 `MT_BOOT_MAX_RANGES (24)` 的严苛限制下，将目标显存切片（Slot 0 & Slot 1）优雅映射至 3D 空间（GPU VA `0x60000000ULL` 与 `0x61000000ULL`，总 range 数 23）；
3. **动态 Render Target 寄存器编解码**：基于 QY1 硬件规范与 UMD 逆向参数布局，在硬件 3D 命令包中动态注入 Render Target 0 虚拟基址、跨距与图块基地址；
4. **用户态全链路渲染目标验证**：在 `mt-3d-check` 中建立完整的 GEM 分配、初始模式写入、3D 渲染执行、Fence 同步与 VRAM 读回核验全流程。

---

## 2. 关键架构实现

### 2.1 并发锁解耦（防止死锁）

原有实现中，`query_ioctl`、`create_ioctl`、`rw_ioctl` 与 `lease_free` 均使用 `submit_lock`。若进程在执行 ioctl 异常退出时，内核 `do_exit` 在调用 `drm_release -> lease_free` 时会发生同线程重入死锁。

r41 引入独立的槽位与元数据保护锁：
```c
static DEFINE_MUTEX(submit_lock); /* 仅保护硬件队列提交与上下文原子切换 */
static DEFINE_MUTEX(slot_lock);   /* 仅保护 GEM 槽位分配、释放与元数据读取 */
```
- `query_ioctl`、`create_ioctl`、`rw_ioctl`、`lease_free` 迁移至 `slot_lock`；
- `submit_lock` 严格专用于 `copy_ioctl`、`fill_ioctl` 与 `submit_3d_ioctl`；
- 彻底消除了进程退出时资源回收的锁冲突。

### 2.2 3D 空间双映射架构与硬件上限控制

S3000 vGPU MMU 单个虚拟地址空间硬编码最大容纳 24 个 mapping ranges。
在 r41 中，`space_3d` 映射分布如下：
- 11 个 3D 上下文 BO 切片：11 ranges
- 1 个 3D 命令包 BO（32KB）：1 range
- 8 个私有切片 + 1 个 boot shared：9 ranges
- 2 个 Render Target 表面切片（Slot 0 @ 0x60000000, Slot 1 @ 0x61000000）：2 ranges
- **总 Range 计数：11 + 1 + 9 + 2 = 23 ranges <= 24**，完美处于物理硬件安全阈值之内！

### 2.3 动态 Render Target 寄存器布局

逆向与 `LinuxGfxOracle` 参数分析验证：
- Linux 3D Universal 命令包全长 `18,160` 字节（`0x46f0`）；
- 寄存器块位于包内末端 `+0x44e0`，其中 Fragment 寄存器起始于 `+0x4590`；
- **Render Target 0 寄存器组**（每个 RT 占 24 字节）：
  - `+0x45a0`：RT0 GPU 虚拟基地址（64 位，指向目标 GEM 显存）
  - `+0x45a8`：RT0 步长与像素格式（64 位）
  - `+0x45b0`：RT0 分辨率与图层范围（64 位，如 1024x1024）
- **Framebuffer 平铺基址**：
  - `+0x4668`：Render Target Framebuffer Base（64 位）

在 `submit_3d_ioctl` 中，当用户态传入 `target_handle` 时：
```c
if (target_lease) {
    u64 rt_va = target_lease->slot->va_3d;
    u64 rt_stride = 1024ULL * 4;
    u64 rt_extent = (1024ULL << 16) | 1024ULL;
    write_bo(&command_3d, 0x45a0, &rt_va, 8);
    write_bo(&command_3d, 0x45a8, &rt_stride, 8);
    write_bo(&command_3d, 0x45b0, &rt_extent, 8);
    write_bo(&command_3d, 0x4668, &rt_va, 8);
}
```

### 2.4 UAPI 演进与二进制兼容性

`include/mt_drm_uapi.h`：
```c
struct drm_mt_submit_3d {
    __u32 out_syncobj;   /* 可选原生 syncobj 句柄 */
    __u32 flags;         /* 提交标志 */
    __u64 frame_tag;     /* 帧序列标识 */
    __u64 sequence;      /* 硬件栅栏序列号输出 */
    __u32 latency_us;    /* 硬件执行延迟采样 (us) */
    __u32 target_handle; /* 可选渲染目标 GEM 对象句柄 (0 表示无) */
};
```
结构体全长严格维持 32 字节，`_Static_assert(sizeof(struct drm_mt_submit_3d) == 32)` 保持 100% 稳定，既有应用程序与新测试工具无缝兼容。

---

## 3. 用户态验证工具升级

升级 `userspace/mt-3d-check.c`：
1. **多帧基准测试**：继续验证多帧（10 帧）批量并发提交、硬件 Ring 消费与纳秒级延迟采样；
2. **渲染目标全链路测试（`check_3d_render_target`）**：
   - 调用 `DRM_IOCTL_MT_CREATE` 创建 64 KiB 目标 GEM 缓冲区；
   - 通过 `DRM_IOCTL_MT_WRITE` 写入特征初始化数据（`0x5a` 清屏）；
   - 调用 `DRM_IOCTL_MT_SUBMIT_3D` 传入 `target_handle` 绑定该缓冲区发起硬件 3D 执行；
   - 验证原生 `drm_syncobj` 与 `sync_file` 异步栅栏通知；
   - 调用 `DRM_IOCTL_MT_READ` 将显存帧缓冲读回用户态，验证 GPU 硬件执行与显存状态；
   - 调用 `DRM_IOCTL_GEM_CLOSE` 正确释放对象。

---

## 4. 结论与下一步

r41 实现了 3D 渲染执行管线与显存帧缓冲区的动态绑定，打通了从用户态分配、GPU 虚拟内存多重映射、硬件 Universal 队列消费、到显存读回核验的完整通路。

下一阶段（r42）：
- 引入硬件顶点缓冲区（Vertex Buffer）与基础几何图元（如单三角形）的命令流组装与光栅化输出验证。
