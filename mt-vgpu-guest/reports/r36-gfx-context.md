# r36：冷启动真机状态恢复与 3D 渲染上下文（Render Context）/ CSW 闭合

2026-09-30。本机完成冷启动后的硬件会话恢复与 1080p GPU 大表面绘图复核；并沿着适配进度完成 r36 阶段：原厂 Linux QY1 3D 渲染上下文（Render Context）、11 个专用 BO 显存规范、12 个上下文保存/恢复任务与 248 字节 CSW 模板的 C 语言闭合与跨平台校验。

## 一、冷启动真机硬件通道恢复与健康复核

本机于 2026-09-30 经历冷重启，新启动 ID 为 `4ce01e8a-a654-4f72-8513-f05e8401c1a8`。

1. **现场排查与状态清理**：
   - 系统开机自动加载的旧 `mtgpu` 因 `PhysHeapsInit [644]` 计数不匹配报错退出，导致 PCI 设备 `0000:00:0e.0` 留存 `driver_state=2, firmware_state=1`。
   - 安全卸载 `mtgpu`；编写并编译冷启动收尾 helper `kernel/recovery/mt_cold_disconnect.ko`，确认 6 个 DM 的所有命令/事件环完全排空（head == tail，started=0，FW=1）；
   - 执行 `finish=1` 安全置 Guest OFF（0），硬件进入标准的 `Guest=0 / FW=READY(1)`。

2. **工作会话建立与真实 GPU 任务复核**：
   - 执行 `fresh-trial.py --runtime-context --run`，成功连接固件并发布运行时上下文，进入 `Guest=2 / FW=2 / started=1 / event_result=0`；
   - 加载 `mt_live_marker.ko`，完成 DM1 空命令真实硬件往返（fence sequence=1，result=0）；
   - 加载 `mt_live_tqx.ko`，完成 256 字节 TQX 显存硬件复制（sequence=2，verified=1，result=0）；
   - 加载 `mt_live_surface.ko`，成功注册摩尔线程 DRM 节点 `/dev/dri/card1` 与 `/dev/dri/renderD128`；
   - 运行 `userspace/mt-surface-check`：
     - smoke 冒烟测试：跨页填充、尾像素填充及 4 MiB 显存复制通过（sequence 3–5，completed=3）；
     - exercise 完整测试：执行 23 次原生 GPU 颜色矩形填充 + 2 次 4 MiB 画面显存复制，完整生成 1080p PPM 读回图像（sequence 6–30，completed=28，verified_bytes=8,388,608）；
   - 截至目前，本机累计完成 30 个真实 GPU 硬件任务，全部正常完成，pending=0，固件保持 ACTIVE。

## 二、r36：3D 渲染上下文（Render Context）与 CSW 闭合

依据 Linux legacy UMD `libsrv_um_MUSA.so.1.0.0` 原始指令执行（`RGXCreateRenderContextCCB` `0017b6c0` -> `001836b0` / `00183c40` / `00183d30` / `00182f40`）：

### 1. 11 个专用 BO 规范与显存需求

| BO 索引 | 名称 / 作用 | 堆类型 | 大小 (字节) | 对齐 | 标志 Flags |
|---|---|---|---:|---:|---:|
| 0 | PDS code/data buffer for DCE context switch tasks | PDS | 3,072 | 128 | 0 |
| 1 | USC shader buffer for DCE context switch tasks | USC | 6,144 | 128 | 0 |
| 2 | DCE context switch snapshot | Component Control | 776 | 32 | 771 |
| 3 | TA state | General | 468 | 32 | 771 |
| 4 | PDS suballocation | PDS | 1,024 | 128 | 0 |
| 5 | PDS suballocation | PDS | 1,024 | 128 | 0 |
| 6 | VDM uniform PDS state | General | 16,400 | 32 | 771 |
| 7 | VDM uniform PDS state | General | 16,400 | 32 | 771 |
| 8 | DDM uniform PDS state | General | 16,400 | 32 | 771 |
| 9 | DDM uniform PDS state | General | 16,400 | 32 | 771 |
| 10 | Rasterisation context state | General | 8,192 | 128 | 2147484419 |
| **合计** | **11 个 BO** | | **86,300 字节** (~84.3 KiB) | | |

> [!NOTE]
> 3D 渲染上下文全部 11 个 BO 总显存开销仅约 **84.3 KiB**。这解决了 r35 中对普通堆剩余空间（约 2 MiB）能否容纳图形上下文的顾虑——84 KiB 的需求完全可以轻松在当前池中满足。

### 2. 12 个上下文保存与恢复任务

原厂 `00183c40` 和 `00183d30` 生成 12 个阶段的任务记录：
- **4 个 PT 阶段**（kind 3，store 1/0）：分别覆盖 PDS 偏移 0..68、68..128、128..196、196..256；
- **4 个 SR 阶段（kind 0）**：覆盖 PDS 偏移 256..440，USC 偏移 0..720；
- **4 个 SR 阶段（kind 1）**：覆盖 PDS 偏移 440..632，USC 偏移 720..1440。

### 3. C 语言生成与跨平台逐字节核验

- 新增 `kernel/mt_gfx_context.h`，定义完整的数据结构、BO 规范、任务表，并实现 `mt_gfx_context_build_csw(...)` 函数，根据 11 个 BO 的 GPU VA 直接构造 248 字节 CSW 块。
- 编写 `scripts/verify-gfx-context.py`，与 Unicorn 原始指令生成的基准结果比对，248 字节 CSW 逐字节 100% 相同（SHA-256: `9557432cfa8a25c17d3d19a4e2300b95ebff40baecb8f67972ba7e1637c35e98`）。
- 编写 `tests/gfx_context_oracle_wrapper.c`，在当前 Linux 6.12 内核头文件下通过 `W=1` 编译，**零 Warning 零 Error**。
- 全套图形回归测试（寄存器、图形包、渲染上下文、内核编译、TQX填充、Linux TDM转换）全部通过。

## 三、后续适配入口

1. 将 11 个图形 BO 的分配器接入内核 GEM 显存管理器，为 3D 渲染建立专用切片。
2. 结合 r35 的图形包编码器与 r36 的渲染上下文，构造首个包含简单三角形或清除阶段的最小有效 3D 工作负载（Minimal 3D Workload）。
