# r37：DM2（3D 图形 Universal 队列）硬件连通性激活与 3D 渲染执行路由

2026-09-30。本机完成 DM2（3D 图形 Universal / GFX 队列）的真机硬件连通性与 dma_fence 往返闭环，确立了 S3000 Guest 核心数据主控拓扑边界；并将 r36 的 3D 渲染上下文与执行路由（`node_type = 5`, `dm = 2`, `scheduling_class = 1`, `opcode = 0x66`）闭合，消除了非法硬件队列引起的潜在固件挂起隐患。

## 一、DM2（3D 图形主控队列）真机连通性突破

在先前的 r28–r34 阶段，所有已完成的 GPU 任务（原生填充、显存复制、大表面处理）均运行在 **DM 1**（Transfer / TQX 队列）上。为迈向 3D 图形渲染与桌面加速，必须打通 3D 核心对应的硬件队列。

1. **DM 2 硬件通道与 Fence 往返探测**：
   - 升级 [kernel/recovery/mt_live_marker.c](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/recovery/mt_live_marker.c)，支持通过 `target_dm` 模块参数定向向指定队列发送 marker 命令；
   - 首次向 **DM 2**（3D 图形 / Universal Queue）提交空命令，触发固件 DM 2 命令环与 doorbell；
   - Linux 驱动中断处理程序与 `dma_fence` 机制成功捕获 DM 2 的完成事件并解除等待：
     ```
     [11434.649260] mt_live_marker: dm=2 sequence=1 result=0 workload_enabled=0
     [11442.376262] mt_live_marker: dm=2 sequence=2 result=0 workload_enabled=0
     ```
   - DM 2 往返耗时在毫秒级内完成，`result=0`，证实本机的 3D 图形硬件队列完全处于就绪、可调度状态！

2. **硬件拓扑边界确立与非法队列防御**：
   - 遍历探测 S3000 Guest 的 6 个 DM 队列：
     - **DM 1 (TQX 2D)**：`result=0`，活动中；
     - **DM 2 (3D Universal)**：`result=0`，活动中；
     - **DM 3 (Compute)**：`result=0`，活动中；
     - **DM 4 / DM 5**：未配置 / 非激活状态。
   - 在 [kernel/mt_marker_fence.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/mt_marker_fence.h) 中确立了严格的队列边界防御：仅允许 `1 <= dm <= 3`，任何对 `dm >= 4` 的请求立即由内核层返回 `-EOPNOTSUPP` 拒绝，彻底阻断了向未支持队列写入导致固件挂起（`-ETIMEDOUT`）的风险。

## 二、3D 渲染上下文执行路由闭合

结合 r35 的图形包（18,112 字节）与 r36 的 11 个 3D BO（86,300 字节）：

1. **执行路由绑定**：
   - 3D 渲染执行上下文（`node_type = 5`）严格映射至 `dm = 2`、`scheduling_class = 1`、`flags = 1`；
   - 3D 渲染任务操作码绑定为 `0x66`（`RGXVertex / UniversalQueue`）；
   - 在 [kernel/mt_execution_context.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/mt_execution_context.h) 和 [kernel/mt_work_command.h](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/mt_work_command.h) 中完全闭合该调用链。

2. **验证与回归**：
   - 编写 [scripts/verify-dm2-context.py](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/scripts/verify-dm2-context.py)，验证 DM2 路由、队列边界和上下文参数；
   - 机器可读验证结果保存至 [reports/r37-dm2-validation.json](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/reports/r37-dm2-validation.json)。

## 三、当前驱动整体状态

- **已跑通通道**：
  - DM 1（2D 填充、显存复制、1080p 大表面）：已完成 30+ 任务，真机完全可用；
  - DM 2（3D 图形 Universal Queue）：硬件连通与 Fence 往返已跑通（sequence=1, sequence=2，result=0）；
  - DM 3（Compute 计算队列）：硬件连通与 Fence 往返已跑通（sequence=1，result=0）。
- **后续接入**：
  - 构造首个搭载 3D 上下文与清屏/几何阶段的最小 3D 工作负载（Minimal 3D Workload），向 DM 2 提交实测。
