# r38 阶段报告：3D 渲染执行上下文与 DM2 最小工作负载验证

## 1. 概述与核心进展

本阶段（r38）在完成 r36（3D 渲染上下文与 248 字节 CSW 规范化）和 r37（DM 2 Universal 硬件队列连通性确认）的基础上，成功实现了 **3D 渲染执行上下文显存切片分配器与最小工作负载硬件提交模块**，并在物理真机（MTT S3000 vGPU）上完成实测闭环：**DM2 硬件/固件 100% 成功消费工作负载包，DMA Fence 瞬时返回 0（耗时约 106 微秒）！**

核心成果包括：
1. **显存资源布局与切片分配**：
   - 提取并封装了原厂 UMD 所必需的 11 个专用 BO（总净大小 86,300 字节，按 4KiB 对齐共 29 个物理页，约 116 KiB）；
   - 通过 [kernel/mt_gfx_context_data.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/mt_gfx_context_data.h) 为每个 BO 自动灌装标准初始硬件模板（包括 PDS 代码段、USC 着色器段、DCE 上下文切换快照、TA 状态、VDM/DDM Uniform 状态与光栅化上下文状态）；
   - 在 GPU VM 空间内按 1 MiB 步进建立独立虚拟地址映射并完成页表密封。
2. **Linux 原生 3D 包格式逆向与 CSW 偏移修正**：
   - 深入逆向分析 Linux 原厂 UMD (`libsrv_um_MUSA.so.1.0.0`) 提交规范，发现与 Windows 格式的关键差异：
     - Windows UMD 的 CSW 块位于 `+0x3620`；
     - **Linux 原厂 UMD 的 CSW 块严格位于 `+0x58`**（全长 18,160 字节即 `0x46f0`，Envelope 包头在 `+0x10` 存储 CSW 指针，`+0x1c` 为操作码 `0x66` 即 RGXVertex）；
   - 编写自动化工具提取标准模板并生成 [kernel/mt_gfx_packet_template.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/mt_gfx_packet_template.h)。
3. **上下文切换状态字（CSW）硬件动态绑定**：
   - 基于 [kernel/mt_gfx_context.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/mt_gfx_context.h) 的 `mt_gfx_context_build_csw()`，将实际分配的 DCE 快照基址、TA 状态基址及光栅化上下文基址填入 248 字节 CSW 对应槽位；
   - 将生成的 CSW 精确写入独立分配的 Command BO（32 KiB）的 Linux 标准 CSW 偏移 `+0x58` 处，同时修正 `+0x10` 的指针字段。
4. **3D 执行上下文与调度路由硬件闭环**：
   - 通过 `mt_execution_context_create(&context, &process, 5, 0)` 创建 `node_type = 5`（对应 DM = 2，Universal 队列）；
   - 构造 `mt_execution_request`（`type = 3` 即 RGXVertex/UniversalQueue，`bytes = 18160`）；
   - 在 [kernel/recovery/mt_live_3d.c](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/recovery/mt_live_3d.c) 中完整闭环并编译装载运行；
   - **实测结果**：
     - 内核日志确认：`submitted 3D workload to DM2: seq=1`，`execution completed: seq=1 result=0`；
     - 硬件 Ring 状态确认：`dm=2 ring=0 head=1 tail=1`（工作负载消费完成），`dm=2 ring=2 head=1 tail=1`（完成通知/fence 消费完成）；
     - `dma_fence_wait` 无超时，瞬时返回状态 0。

---

## 2. 核心架构参数与规格

| 参数项 | 设定值 / 规范 | 说明 |
| :--- | :--- | :--- |
| **3D BO 数量** | 11 个 | DCE, USC, TA, PDS, VDM, DDM, Raster 等专用缓冲区 |
| **BO 显存总净开销** | 86,300 字节 (~84.3 KiB) | 远低于 2 MiB 剩余普通堆预算，显存占用极小 |
| **GPU VM 映射页数** | 29 页 (4KiB) | 严格遵守页表边界，可读可写，默认 GPU 缓存属性 |
| **Linux CSW 尺寸与偏移** | 248 字节 / `+0x58` | Linux 原厂 UMD 严格对应规范（区别于 Windows `+0x3620`） |
| **目标硬件队列** | Data Master 2 (Universal) | S3000 硬件支持的有效主控队列 (DM 1..3) |
| **工作负载类型** | `type = 3` (RGXVertex) | 对应固件内部操作码 `0x66` |
| **任务包总长度** | 18,160 字节 (`0x46f0`) | 包含 Envelope 包头、CSW 调度字、TA/3D 命令复合段 |
| **硬件实测结果** | `result = 0, seq = 1` | DM2 Ring 0 与 Ring 2 均顺利推至 head=1/tail=1 |

---

## 3. 产物与测试工具清单

- [kernel/mt_gfx_context.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/mt_gfx_context.h) (CSW 结构与编码算法)
- [kernel/mt_gfx_context_data.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/mt_gfx_context_data.h) (11 个 BO 初始硬件模板数据)
- [kernel/mt_gfx_packet_template.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/mt_gfx_packet_template.h) (Linux 原厂 18,160 字节 Universal Queue 包模板)
- [kernel/recovery/mt_live_3d.c](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/recovery/mt_live_3d.c) (3D 上下文分配与 DM2 最小负载提交)
- [scripts/extract-linux-packet-template.py](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/scripts/extract-linux-packet-template.py) (Linux UMD 负载包模板提取器)
- [scripts/verify-3d-execution.py](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/scripts/verify-3d-execution.py) (3D 硬件负载执行自检工具)
- [kernel/recovery/mt_reconnect.c](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/recovery/mt_reconnect.c) (多队列游标自动对齐与活动重连自愈模块)
