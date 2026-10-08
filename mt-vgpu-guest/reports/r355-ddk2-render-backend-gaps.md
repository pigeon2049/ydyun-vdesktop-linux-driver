# r355：真实 DDK2 render backend 缺口盘点——`0x82:0xC`（MUSAKICKGFX2）是 UMD TA 路径内实际发出的调用，桥侧未实现（离线盘点，零硬件触碰）

## 结论（盘点）

1. **新发现（实测支撑）**：r354 的崩溃证据（`0x92930(rdi=0x6000, rsi=0x82, rdx=0xc)`）表明，UMD 的 TA 路径在其内部（`ZeusSyncPrimImportFD` 下游经 `0x36ec0`）实际发出 **`0x82:0xC` 桥调用**。对照 5.2 生成头（`reference/kmd-5.2.0-server-generated/common_musagfx_bridge.h`），`0x82:0xC = MUSAKICKGFX2`（TA/3D/PR 提交，富结构：`pui8TACmd/pui83DCmd/pui83DPRCmd`、sync prim 块、fence、ZS buffer）。桥侧 dispatch（`kernel/recovery/mt_pvr_bridge.c`）的 0x82 组无 0xC 定义，走 `default: return -ENOTTY`——**缺失**。
2. **STATUS 口径修正（盘点）**：STATUS.md 曾把 `0x82:0xC` 与 `0x81:0x5` 并列为"S4 真提交边界、待真实工作包"。r354 给出 UMD 侧真实调用点与 wire 结构，`0x82:0xC` 从"边界"升级为**具体可排的下一个桥目标**（调用点已知、结构已知、S4 属性不变——仍是真实提交，需按执行路径实现，不得 accept-and-log 代替）。
3. **`0x81:0x5` 维持 S4 边界**：仍被拒绝（`-ENOTTY`），无 UMD 侧调用证据，本轮不列为缺口。

## DDK2 现状三分法（实测：读源码 + dispatch 表）

### A. 已真实实现

| 路径 | 状态 | 依据 |
|---|---|---|
| SRVCORE(0x1)：Connect/事件/多核/info页 | 真实/桩-ok | dispatch 4396–4418；`pvr_cmd_connect` 真实 |
| SYNC(0x2)：AllocSyncPrimitiveBlock、SyncPrimCpuSignal | 真实 | r222 实测写回 |
| MM(0x6)：PMR 分配/导入/映射、devmem ctx/heap、heap 配置 | 真实 | dispatch 4434–4470 |
| 0x88:0x4 RGXKickSync3 | 翻译执行 | `translate_kick=1` 转真实 DM2 空 marker（r62/r63、r148/r149、r212/r213 活体验收） |
| TQX translator fire（0x89:0xa 侧） | 真实 GPU 执行 | translator 自有 TQX context，分块 fire，像素验过（r267–r292，r290 UMD 驱动首绿） |
| MUSAKICKGFX5 wire schema（108B） | 已钉 | `mt_pvr_wire.h`（r204–r207，5.2 Host 头哈希对版） |
| GFX context 规格 | 规格存在 | `mt_gfx_context.h`（11 BO/PDS-USC）、`mt_gfx_packet.h`（未接 firmware） |

### B. Accept-and-log 空桩（点名；STATUS 禁止其代替执行）

| 路径 | 现状 |
|---|---|
| 0x82:0x14 RGXKICKTA3D5（MUSAKICKGFX5） | `pvr_cmd_kickta3d5_observe`：报 CCB 窗口、回 0、不执行（r215） |
| 0x89:0xa RGXTDMSubmitTransfer3 | `pvr_cmd_tdm_submit3_observe`：同上模式（r174） |
| 0x82:0x12 / 0x88:0x5（DDK2 render/CCB create） | 只 mint handle token，IN opaque，无 server 侧状态 |
| 0x89:0x8/0x9（DDK2 transfer ctx） | bookkeeping token，无 firmware context |

### C. 缺失

| 缺口 | 说明 |
|---|---|
| **0x82:0xC（MUSAKICKGFX2）** | 桥侧 `-ENOTTY`；UMD TA 路径内真实发出（r354）；wire 结构已知（5.2 头） |
| TA/3D firmware 提交通道 | probe 只有 `submit_tqx_work`（`kernel/mt_marker_fence.h:27`）；无 TA submit op |
| Per-file GPU VM/BO 后端 | r208 已划边界：当前 per-file VM 是 CPU-only planning facade；borrowed-BO 路径已设计未实现 |
| UMD 侧真实建连 | UMD 自带完整 `PVRSRVConnectionCreateDevice`（0xd0 连接结构，`FUN_0013b7c0` open `/dev/dri/renderD*`，对齐检查）；fabricated harness 一直绕过它 |

## 从 r354 倒推的 backend 需求清单（按依赖排序）

- **R1（UMD 建连，client 侧）**：走 UMD 真实 `PVRSRVConnectionCreateDevice` 路径（open renderD128 → 0xd0 结构 → `GetFeatures` → 对齐检查），使 `GetSrvHandle` 返回有效 fd 指针。桥侧 `pvr_cmd_connect` 已就绪，无需新桥代码。语料：`GetSrvHandle @ 0x13c1c0` = `rdi ? *(uint64_t*)rdi : 0`（SHA `b3058c02…34237b0` 对版）；`FUN_00192930` 即 `PVRSRVBridgeCall`（`ioctl(*param_1, 0xc0206440, …)`，与桥侧 `static_assert(DRM_IOCTL_PVR_BRIDGE == 0xc0206440)` 同值——UMD 与桥的 ioctl 号已对齐）。
- **R2（0x82:0xC 桥实现）**：wire 结构入库（5.2 头 `MTGPU_BRIDGE_IN_MUSAKICKGFX2_TAG`）→ observer 占位 → 执行/翻译。S4 属性：必须真实执行或翻译，不得 accept-and-log。
- **R3（0x82:0x14 真实执行）**：解析 108B MUSAKICKGFX5（schema 已钉）→ 定位 CCB 窗口（server 侧持有，r56–r57）→ 构建 TA/3D 提交 → firmware 通道提交 → fence 完成语义。
- **R4（TA 提交通道）**：probe 侧新增 TA submit op（或扩展 marker 机制）；TQX 的 `submit_tqx_work` 是模板。
- **R5（Per-file VM/BO，r208）**：borrowed BO、per-file 真实 VM、execution process/render context、PMR VA 绑定、CCB 资源闭包。
- **R6（DDK2 context 状态）**：0x82:0x12/0x88:0x5 从 handle token 到 server 侧状态（firmware context、CCB 管理）。
- **R7（Sync prim 导入路径）**：`ZeusSyncPrimImportFD` 下游在真实建连后应走通；桥侧 SYNC 命令多已实现，需 R1 完成后验证。

## 分轮工作分解（r356+，提案；每轮一件事）

1. **r356（离线）**：0x82:0xC wire 结构入库（`mt_pvr_wire.h`，仿 0x82:0x14 注释风格）+ dispatch 接 observer 占位（明确标注非执行）；门禁钉住 IN/OUT 尺寸。*前置：无。*
2. **r357（离线 fabricated）**：UMD 真实建连 recon——GDB 走 `PVRSRVConnectionCreateDevice` 真实路径，确认 `GetSrvHandle` 返回有效指针、`PVRSRVBridgeCall` 到达桥侧 `pvr_cmd_connect`。不提交 GPU 工作。*前置：R1。*
3. **r358（需批准·活体）**：0x82:0xC observer 活体观察——真实 UMD TA 路径下记录 0x82:0xC 到达与 IN 参数，对照 5.2 结构。*前置：r356。*
4. **r359（离线）**：TA firmware 提交通道设计（复用 marker/TQX 机制或新增 op），对接 r208 per-file VM 需求。*前置：R4/R5。*
5. **r360（离线）**：0x82:0x14 执行翻译设计——MUSAKICKGFX5 → TA/3D 提交，CCB 窗口定位、sync prim 关联、fence 语义。*前置：r358 实测。*
6. **r361+（需批准·活体）**：按 r359/r360 设计实现执行路径并验收。

## 实测与边界

1. 全程离线：读源码、dispatch 表、语料（SHA 已核对）、5.2 生成头；零硬件触碰，会话 freeze 未动。无模块加载、无 GPU 工作。
2. 推断已标注：R2–R7 的实现顺序为提案；0x82:0xC 的"S4 升级"基于 r354 单次 fabricated 观测 + 5.2 头对照，活体复核在 r358。
3. 未断言：`ZeusSyncPrimImportFD` 内 0x82:0xC 调用的完整参数语义（IN 指针内容未在 fabricated 下捕获）；`FUN_0013b7c0` 之后 fd 指针在 0xd0 结构中的确切偏移（GDB 活体可定，r357 做）。

## 门禁

- 本轮纯盘点、无代码改动：核对引用路径存在（`kernel/recovery/mt_pvr_bridge.c`、`kernel/mt_pvr_wire.h`、`reference/kmd-5.2.0-server-generated/common_musagfx_bridge.h`、语料 `decompiled/linux-legacy-umd-5.2.0/` 均存在）。
- `make -C mt-vgpu-guest check-offline` 全绿（见下）。
