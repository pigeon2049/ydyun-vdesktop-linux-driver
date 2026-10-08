# r379: 0x82:0x14 (MUSAKICKGFX5) 现状为 accept-and-log，执行路径设计完成

## 结论

`0x82:0x14`（RGXKICKTA3D5 / MUSA:MUSAKICKGFX5）的 wire 结构已入库（108B IN / 4B OUT），但桥侧仍为 r215 的 **accept-and-log observer**（`pvr_cmd_kickta3d5_observe`），未真实执行。本轮完成执行路径设计：DM2（3D引擎）、新增 `mt_marker_ops` 第6 op、`submission_va` 经 R5 per-file VM 映射。关键未知（firmware opcode、完成事件格式、submission 解析）列为 V1–V4 待活体验证。

## 1. 现状盘点

### 1.1 桥侧处理：accept-and-log observer（r215）

- Dispatch：`MT_PVR_FN_RGXKICKTA3D5` → `pvr_cmd_kickta3d5_observe()`（`mt_pvr_bridge.c:4872`）
- 行为：解码 IN → 校验 `submission_va`/`submission_size` 范围 → 查找 `render_context` → 报告 CCB 窗口 + check/update/sync-PMR counts → **返回 0**
- **不执行**：不解引用 userspace 指针数组、不 mint fence、firmware/DMA/translator 路径不可达
- 注释原文："Real TA/3D execution still needs the DDK2 render backend (r207/r208 boundary)"

### 1.2 Wire 结构已入库（`mt_pvr_wire.h:311`）

`struct mt_pvr_rgxkickta3d5_in`（108B）/ `struct mt_pvr_rgxkickta3d5_out`（4B，仅 error）。

来源：hash-verified 5.2.0 Host generated header（`reference/kmd-5.2.0-server-generated/common_musagfx_bridge.h:574`）。注释明确：2.7.1 Native 头短 12 字节（无 `ui32SubmissionFlags`/`ui64SubmissionId`），不是此 wire 布局。

| 偏移 | 字段 | 语义 |
|---|---|---|
| 0 | `render_context` (u64) | Render context 句柄 |
| 8–32 | `check_sync_prim_blocks`/`check_sync_offsets`/`check_values` (u64×3) | Check fence 数组指针三元组 |
| 32–56 | `update_sync_prim_blocks`/`update_sync_offsets`/`update_values` (u64×3) | Update fence 数组指针三元组 |
| 56–72 | `sync_pmr_flags`/`sync_pmrs` (u64×2) | Sync PMR 数组 |
| 72 | `submission_flags` (u32) | Submission 标志 |
| 76 | `submission_va` (u64) | 命令缓冲 VA（umd_bridge_shim.c:1032 确认偏移） |
| 84 | `submission_size` (u32) | 命令缓冲尺寸 |
| 88 | `submission_id` (u64) | Submission ID |
| 96–108 | `check_count`/`update_count`/`sync_pmr_count` (u32×3) | 数组计数 |

### 1.3 与 0x82:0xC 的关键差异

| 维度 | 0x82:0xC (MUSAKICKGFX2) | 0x82:0x14 (MUSAKICKGFX5) |
|---|---|---|
| IN 尺寸 | 268B | 108B |
| OUT 尺寸 | 12B（error + update_fence + update_fence_3d） | 4B（仅 error） |
| Kick 模型 | TA+PR+3D 分离（kick_ta/kick_pr/kick_3d 开关） | 单一 submission |
| 命令缓冲 | ta_cmd / 3d_cmd / 3dpr_cmd 三路 | submission_va 单路 |
| 上下文 | 隐式（无 context 字段） | render_context 显式句柄 |
| Fence 回填 | OUT.update_fence = wire_id（r373 已验证） | **无**（OUT 仅 error） |
| 当前状态 | 真实执行（r367 起） | accept-and-log（r215） |
| DM | 3（TA，r365 实测） | **2（3D，推断，待验证）** |

**核心差异语义**：`0x82:0xC` 是 TA 引擎的精细控制（三分路 kick），`0x82:0x14` 是 DDK2 的统一提交接口（单一命令缓冲 + render context）。"TA3D" 之名指其覆盖 TA+3D 流水线，非指走 TA DM。

## 2. 执行路径设计

### 2.1 DM 分配：DM2（推断 → V1 验证）

- DM1 = TQX（`mt_marker_fence.h:232` 实测）
- DM2 = 3D（`mt_live_3d.c:7`："Submits ... to DM2"，实测）
- DM3 = TA（r365 实测）
- `0x82:0x14` 的 `render_context` + 单 `submission_va` 语义指向 3D 引擎 → **DM2**
- 若 firmware 拒收，回退探测 DM3（TA 引擎亦可能接受统一提交）

### 2.2 框架复用：`mt_marker_ops` 第 6 个 op

新增 `submit_3d_work`（与 `submit_ta_work` 并列，不动现有 3D 路径）：
- `struct mt_3d_submit_params`：`submission_va`/`submission_size`（decode 时捕获 userspace 指针）、`check_*`/`update_*` 数组三元组、`render_context`、`submission_id`/`submission_flags`
- IN→params 映射：`submission_va` 经 R5 per-file VM 映射为 firmware 可见 GPU VA（复用 r374/r375 基础设施）；check 数组 → `check_fence` 输入等待；update 数组 → 完成时写值
- Firmware 命令 opcode：**待定**（TA 用 `0x66`；3D 的 opcode 需从 Windows KMD 或活体探测确定 → V2）

### 2.3 Fence/Sync 语义

- **输入**：`check_*` 数组（count + 指针三元组）→ 沿用 marker 框架的 `check_fence` 等待路径（同 TA D6）
- **完成**：`update_*` 数组写值 → `dma_fence_signal`（r159 语义）
- **差异**：OUT 仅 4B error，**无 `update_fence` 回填**。Userspace 获知完成的方式待确认（可能经 sync prim 轮询，或 DDK2 有独立查询接口 → V4）
- **错误处理**：照抄 TA D7（硬件写前全校验、队列满 `-EAGAIN`、wire_id 永不复用）

### 2.4 与 R5 的关系

`submission_va` 指向 userspace 命令缓冲，需经 R5 per-file VM 映射（`mt_ta_vm_map_cmd_buffer` 泛化为 `mt_vm_map_cmd_buffer`，或新增 3D 专用映射）。`MT_TA_VM_READY` 门逻辑复用。

## 3. 状态标注

- **[MEASURED]**：wire 结构（KMD 5.2.0 头，hash-verified）；observer 行为（源码）；DM1=TQX、DM2=3D、DM3=TA（各有实测）；`submission_va@76`/`submission_size@84`（umd shim）；OUT 无回填（4B 结构）
- **[INFERRED]**：DM2 分配（语义推断）；第 6 op 设计（仿 r366 模式）；fence 语义类比 TA
- **[TO-VALIDATE]**：V1 DM2 接受性；V2 firmware opcode；V3 完成事件格式；V4 submission 解析格式 + userspace 完成通知机制

## 4. 待活体验证清单

- **V1**：DM2 是否接受 `0x82:0x14` submission（单发空 marker，同 r365 模式）
- **V2**：Firmware 期望的 3D opcode（若 V1 用占位 opcode 被拒，从 Windows KMD 抓）
- **V3**：DM2 完成事件格式（是否如 TA 的 `0x100` 特异码，或标准 COMPLETE）
- **V4**：`submission_va` 命令缓冲的 firmware 解析格式；OUT 无回填时 userspace 如何获知完成

## 5. 诚实边界

- 本轮**未写任何实现代码**，未触碰硬件（离线）
- Wire 格式来自 KMD 5.2.0 生成头（可信），但**未在活体验证** `0x82:0x14` 的实际 IN 内容
- DM2 分配为语义推断，需 V1 单发验证（同 r365 的 DM3 验证模式）
- 不得用 accept-and-log 代替执行——设计的是真实执行路径（r215 observer 将被替换）
