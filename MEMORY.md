# MEMORY — 摩尔线程 vGPU 驱动适配

最后更新：2026-09-30（提交 `ee1dd8d`，阶段 r42 完成）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 目标

在 Debian 13.7 / Linux 6.12.107 虚拟机中，为摩尔线程 S3000 vGPU
（PCI `1ed5:0222`，subsystem `1ed5:1101`，Guest 设备 `0000:00:0e.0`）
自研 Linux 原生驱动，最终实现可用的图形加速（GL/Vulkan/合成器）。

当前显示仍为 QXL + llvmpipe 软件渲染。GPU 硬件通路已打通并可执行任务，
但距离可用图形栈还差关键环节（见「关键阻塞」）。

---

## 本次会话进展（r42，提交 ee1dd8d）

### 1. 订正了 r41 的一处错误结论

r41 报告与 `PROTOCOL-NOTES.md` 把「S3000 vGPU MMU 单个虚拟地址空间最多容纳
24 个 mapping ranges」记为**硬件物理上限**。核查确认这是**驱动自己的编译期常量**：

- `MT_BOOT_MAX_RANGES 24U` 唯一实际用途是 `struct mt_gpu_vm` 中
  `bindings[24]` 内联数组的长度，以及 `mt_gpu_vm_plan()` 里两个同长的**内核栈数组**；
- 真实 MMU 是三级页表 walk（`mt_mmu.h`，逆向自 `mtkm64.sys`）：
  PC 1024×4B（每项 1 GiB）→ PD 512×8B（每项 2 MiB）→ PT 512×8B（每项 4 KiB）；
- **硬件对映射数量没有任何上限**。单个地址空间的实际上限来自页表页预算，
  即 `MT_BOOT_MAX_MAPPED_BYTES`（64 MiB）对应的 16384 个页表项；
- 任何真实图形栈（需要成百上千个 buffer 映射）都过不了 24 这一关，
  所以这是前置阻塞项。

### 2. 上限改为从页表几何推导

`mt_mmu_bootstrap.h` 新增 `mt_boot_max_ranges(table_pages)`：
`min((table_pages - 1) × 512, MT_BOOT_MAX_MAPPED_BYTES / 4096)`，root 页不可用于映射。

- 生产配置（32 页）实测上限 **15872**，而非 24；
- `mt_mmu_build_pages()` 改为按调用方实际页预算校验，
  页预算耗尽仍由既有循环返回 `-ENOSPC`（那才是贴近硬件的真实边界）。

### 3. 映射表与规划暂存移出内联数组和内核栈

- `struct mt_gpu_vm.bindings[24]` → 按需增长的堆指针；
- 新增 `ranges` / `page_lists` 两个暂存数组，同样堆分配。
  **必须这样做**：直接把 24 改成 15872 内联会让结构膨胀到约 500 KB，
  且每次 `mt_gpu_vm_bind_many()` 在 16 KB 内核栈上压入两个大数组，直接爆栈；
- `mt_gpu_vm_grow()` 按 2 的幂增长，稳态不重复分配；分配失败保留旧数组与旧
  `count`，被拒绝的 bind 不污染既有 plan；
- `mt_work_job.bos[]` 同样是按映射数定长的内联数组，一并改为精确分配的指针。

### 4. 事务性语义与 errno 保持不变

- `bind_many()` 改为**先整批校验、再 grow、最后原地暂存到尾部**；
  重叠检测从 planner 内部提升到校验循环，故冲突请求既不增长数组，
  也不改变 image / count / 页数 / 任何引用计数；
- 范围预检顺序与 planner 自身一致（`va >= 1<<40` → `-EINVAL`，
  `bytes > (1<<40)-va` → `-ERANGE`），调用者观察到的 errno 不变；
- `mt_gpu_vm_commit()` 改为只清空退役尾部（原整体 memset+memcpy 与原地暂存矛盾）；
- 修正陈旧指针隐患：`plan()` 中 `page_lists[i]` 原先仅在 BO 有 `page_pa` 时写入，
  否则沿用上次 plan 的值——改为复用暂存数组后这会成为真实映射错误来源，
  现改为无条件赋值。

### 5. 补全共享 ABI 校验（原本正是它放过了本次变更）

`scripts/verify-runtime-integration.py` 原先只比对 `mt_guest` 一个结构。
`mt_guest_device` 内嵌 `address_spaces`，所有 recovery 模块都直接读主模块分配的
`mt_gpu_vm` / `mt_vm_vram` 字段——本次 `mt_gpu_vm` 布局变更正是因此被静默放过。

现对 7 个共享结构（`mt_guest`、`mt_guest_device`、`mt_gpu_vm`、`mt_vm_binding`、
`mt_vm_vram`、`mt_vm_store`、`mt_work_job`）取 pahole 摘要、
写入 `reports/shared-abi-baseline.json` 并在后续构建上门禁。

### 6. UAPI

`struct drm_mt_query` 尾部追加三个只读字段（56 → 80 字节，既有偏移不变）：

```c
__u64 vm2d_mappings, vm3d_mappings;
__u64 vm3d_max_mappings;
```

目的：让用户态可直接看到地址空间余量而不必猜测。
`mt-3d-check` 增加断言 `vm3d_max_mappings > 24`。

### 7. 验证结果

- 新增 `tests/gpu_vm_scale_test.c`（ASan + UBSan）：
  - 推导上限（root 页不可映射、2 页 = 512 项、页几何 vs mapped-byte 取小、
    `capacity < 8192` 时为 0 并被 `init` 拒绝、超上限 capacity 收敛）；
  - **512 个同时存在的映射**（旧上限 21 倍），共占 6 个页表页，
    对全部 512×4 个页面做**独立三级 walk 逐页核对**物理地址；
  - 拒绝路径不变式：重叠 / 非对齐 / 越界 / 非法 flags / 外部 store /
    页预算耗尽，逐项确认 image、count、页数、**数组容量**、引用计数全部未变；
- 全量 21 个测试通过；`kernel/`、`kernel/recovery/`、`kernel/selftest/`
  三处 `W=1` 零警告零错误（`mt_drain_pending.c` 与 `mt_fw_event_io.h`
  的 3 条警告是既有的，未改动时同样存在）；
- **ASan 抓到一处真实泄漏**：`mt_work_job_prepare()` 在 `mt_bo_gpu_begin()`
  失败回滚时只释放了 BO pin，漏掉了新分配的 pin 数组。已修。

### 8. 真机验证未完成（原因明确，需用户决定）

当前 `mt_guest_probe` 仍在运行，且是**用旧 `mt_gpu_vm` 布局编译的**
（内联 `bindings[24]`，`sizeof` 与字段偏移均不同）。recovery 模块按新布局
访问主模块分配的对象，`space_2d->vm.max_ranges` 读到 0，
`create()` 在 `mt_gpu_vm_init()` 处失败，insmod 报 `-EBUSY`。

**要完成真机验证必须重新加载主模块，这会销毁已建立的固件会话**
（Guest=2 / FW=2 / started=1），属影响硬件状态的操作，未擅自执行。

---

## 关键阻塞：为什么不是三角形光栅化

核查 11 个 3D 上下文 BO 的实际内容（`kernel/mt_gfx_context_data.h`）：

| BO | 字节 | 非零 | 内容 |
| --- | --- | --- | --- |
| 0 | 3072 | 227 | PDS 上下文切换程序 |
| 1 | 6144 | 1216 | USC shader |
| 2 | 776 | **0** | DCE context switch snapshot |
| 3 | 468 | **0** | **TA state** |
| 4, 5 | 1024 | **0** | PDS |
| 6, 7 | 16400 | **0** | **VDM uniform PDS state** |
| 8, 9 | 16400 | **0** | **DDM uniform PDS state** |
| 10 | 8192 | **0** | **Rasterisation context state** |

**TA state、光栅化上下文、以及全部 VDM/DDM 程序全为零。**
当前 3D 上下文只有「上下文切换」能力，没有「绘制」能力。
硬件能消费包并返回 `result=0`，**不等于发生了光栅化**。

原因：`FUN_00183d30` 的原厂错误字符串即
`RGXGenerateContextSwitchUniformTasks: Failed to create USC task`。
它只负责生成 uniform 存取程序，且因上下文描述符全零而直接失败。
真正的顶点取数程序由原厂 **PSC 编译器闭包**（`001a4fc0`..`001b6e40`）现场生成，
不是可以手写常量填入的模板。

**r41 文档写的下一步「顶点缓冲 + 单三角形光栅化」被这条硬依赖挡住。**
可达路径只有两条：

1. 逆向重实现 PSC 编译器以合成顶点取数程序；
2. 实现 PVRSRV ioctl 桥接，让原厂 Linux UMD 的渲染路径跑在自研驱动上。

**建议路径 2**——这是通往可用图形栈（GL/Vulkan/合成器）的唯一正确道路，
但工作量远大于 r42，需要单独做阶段设计。

---

## 辅助工具

- `scripts/trace-packet-field-map.py`：对原厂 Linux UMD 逐 word 差分探测
  （121 个可达 word），把 5 个描述符字段映射到包内偏移，
  落盘 `reports/packet-field-map.json`。
  这是后续定位顶点/图元寄存器字段的工具基础，
  但它给出的是**数据依赖，不解释寄存器语义**，不要当作解码结果使用。
- `scripts/verify-runtime-integration.py`：全量构建 + 21 个 RAM 测试 + ABI 门禁。
  运行方式：`cd mt-vgpu-guest && python3 scripts/verify-runtime-integration.py`

---

## 常用命令

```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest

# 全量离线验证（21 测试 + W=1 构建 + ABI 门禁）
python3 scripts/verify-runtime-integration.py

# 内核构建
make -C kernel W=1 && make -C kernel/recovery W=1 && make -C kernel/selftest W=1

# 用户态工具
make -C userspace

# 真机状态（需要主模块已按新布局重载）
sudo insmod kernel/recovery/mt_live_3d_drm.ko
sudo ./build/userspace/mt-3d-check 5
sudo rmmod mt_live_3d_drm
```

注意：r42 之后，真机命令在**主模块按新布局重新加载之前**会失败（`-EBUSY`）。

---

## 下一步（待用户决定）

1. **是否重新加载主模块以完成 r42 真机验证？**
   会销毁当前固件会话，需用户明确同意。
2. **选定三角形/渲染路径**：逆向 PSC 编译器，还是走 PVRSRV ioctl 桥接
   让原厂 UMD 跑渲染路径（建议后者）。
3. 若走 UMD 路径，需先做阶段设计：梳理原厂 UMD 对内核侧
   `PVRSRV` ioctl 的完整需求面。

---

## 已完成阶段索引

| 阶段 | 内容 |
| --- | --- |
| r22b | V2 / OSID 6 协商、显存堆重建、MMU 根上下文、页表映射 |
| r23–r31 | TQX 2D 通路、LMA 读写、firmware 连接、显存保留池 |
| r32–r34 | 独立 DRM 节点、GEM 显存管理、原生 GPU 填充与大表面 |
| r35–r38 | 3D 寄存器与 18112 字节任务包、CSW 状态字、DM2 Universal 队列闭环 |
| r39 | 多帧批量压测、Ring 回绕验证（106 帧 100% 成功） |
| r40 | 用户态 DRM 接口、2D/3D 双 VM 空间隔离 |
| r41 | 3D Render Target 动态绑定与 VRAM 读回（真机 20 帧成功） |
| **r42** | **GPU VA 映射可扩展化，移除 24 mappings 假上限（本次）** |

---

## 注意事项

- `mt_guest_probe` 仍在运行，固件会话已建立，本次**未触碰**；
  内核 taint 为 12800（外部/未签名模块），与进入本会话时一致。
- 用户已加入 `video` 与 `render` 组，权限不是问题。
- 实验副本与临时模块不得在移除入口保护后直接试载。
- 未验证事项见 `reports/r42-vm-mapping-scale.md` 第 6 节。
