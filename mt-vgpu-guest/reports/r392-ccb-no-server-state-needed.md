# r392：R6-5 调研结论——server 侧 CCB 不需要真实化，`0x88:0x5` 空 token 已足够（离线，零硬件触碰）

## 结论

**R6-5（CCB server 侧分配与管理）评估完成：不需要实现。** `0x88:0x5`
（`BridgeRGXCreateKickSyncContext2`）当前的空 token 实现（mint
`MT_PVR_KIND_KICKSYNC`，IN 丢弃）对本桥架构已足够。r355/r56 提出的"缺口"
（context 对象应 = {CCB 设备内存 + 同步状态}）基于 **CCB-ring 提交模型**
假设；而本桥的 DDK2 路径采用**直接固件包构造**（r366/r367 TA、r382 3D），
根本不经过 CCB ring。r144 活体已证明 UMD 的 CCB 全生命周期
（create→destroy）在空 token 下全部返回 0、零崩溃。

**工作项状态**：R6-5 关闭为 **won't-do by design**（有据可查，见下）。
若未来要实现 legacy CCB-ring 直通模型，可重开。

## 1. CCB 的准确定义

**CCB = 命令环**（r56 原文"CCB（命令环）"；PowerVR/RGX 语境下即
Client/Circular Command Buffer）。它有两面：

| 面 | 位置 | 内容 | 谁分配 |
|---|---|---|---|
| UMD 侧 | 用户态 | `SubmissionBufAllocator`（0x50B 对象，render context `+0x200`，r199）+ `PVRSRVAllocUserModeMem` 的用户态 CCB 缓冲（r56） | UMD（`RGXCreateKickSyncContextCCB` → `SubmissionBufAlloctorCreate`） |
| Server 侧 | 设备内存（firmware 可见） | 命令环 ring buffer：UMD 写入 kick 命令，firmware 控制流处理器读取 | Windows KMD 在 context 创建时内部分配（r56 rung6 证据：无 UMD 可见的 PMR/heap 增发） |

Wire（KMD 5.2.0 生成头 `common_musakicksync_bridge.h`，实测）：

- Legacy `MUSACreateKickSyncContext`（0x88:0x0）：IN 16B
  `{hPrivData, ui32ContextFlags, ui32PackedCCBSizeU88}`（r56 合成路径全零捕获）
- DDK2 `MUSACreateKickSyncContext2`（0x88:0x5）：IN **8B** `{hPrivData}`
  ——flags 与 packed CCB size 在 DDK2 变体中被 drop，server 自行决定

## 2. Linux 侧现状（实测：读源码）

`kernel/recovery/mt_pvr_bridge.c`：

- `pvr_cmd_kicksyncctx2_create`（:2787）：`pvr_in` 8B → `pvr_object_new(file,
  MT_PVR_KIND_KICKSYNC)` → OUT 12B `{handle, error}`；`(void)in` 丢弃
  `priv_data`。与 legacy `pvr_cmd_kicksync_create`（0x88:0x0，:2762）同对象模型。
- `pvr_cmd_kicksync_destroy`（:2806）：`pvr_object_find` 存在性校验 →
  `list_del` + `kfree`。
- `pvr_cmd_kicksync_submit`（:2605，0x88:0x2/3/4）：`pvr_object_find` **仅做
  存在性校验**（`if (!obj) return -ENOENT`），随后走 `pvr_translate_kick`
  （client sync-prim 数组 → UFO 条件 → dma_fence）或 inspect；mint 一个已完成
  的 fence fd。**不读写任何 CCB ring。**
- `0x82:0xC`（`mt_pvr_musakickgfx2_in`，`mt_pvr_wire.h:362`）与 `0x82:0x14`
  （`mt_pvr_rgxkickta3d5_in`，`:323`）：IN 结构中**没有 kicksync 字段**，
  只取 `h_render_context`。r391 的 kick 解析只用 render_ctx 的 VM，
  与 kicksync 对象无交集。

即：本桥没有任何执行路径消费 kicksync context 的 server 侧状态。

## 3. 是否需要真实化：不需要（有据）

**[实测]** r144（活体）：`SubmissionBufAlloctorCreate` 崩溃的直接原因是
harness 传参差一层间接（`&b5` vs `b5*`），**不是桥缺口**。修正后 DDK2 CCB
全生命周期 `0x82:0x12 → 0x88:0x5 → 0x88:0x6` **全部返回 0、harness exit 0、
零崩溃**。空 token 已满足 UMD。

**[实测]** r56（rung6）：kicksync context 创建未增发任何 PMR/heap →
CCB 不在 UMD 可见对象里 → UMD 无法 mmap 它 → UMD 不可能依赖 server 侧
CCB 内存的可映射性。

**[实测]** r199：UMD 需要的"CCB 管理"（`SubmissionBufAllocator`）是纯用户态
对象（render context `+0x200`，0x50B），不经过桥。

**[架构]** 本桥 DDK2 路径（r366/r367/r382）：内核直接构造 firmware 工作包
（DM3/opcode `0x66`、DM2/opcode `0x68`）经 DM 提交——**绕过了 CCB-ring
提交模型**。Server 侧 CCB ring 是该模型的传输细节；模型既已替换，
其 server 侧分配即无消费者。

**[推断]** Windows KMD 确实在 server 侧分配了 CCB 设备内存（r56 的解读），
但那是它的实现内务；Linux 桥只要 UMD 行为一致（r144 已证），就不必复制
该内务。

## 4. 与 R6 的关系：独立

- R6（render context，0x82:0x12）：11 BO + CSW + firmware 执行上下文，
  被 `0x82:0xC`/`0x82:0x14` 经 `h_render_context` 消费（r389–r391）。
- R6-5（kicksync/CCB context，0x88:0x5）：仅被 legacy `0x88:0x2/0x3/4`
  kick 做存在性校验；DDK2 kick 完全不引用它。

两者无依赖、无共享状态。R6-5 的结论不影响 R6-1~R6-4，R6-6（TDM）
亦独立。

## 5. 是否阻塞真实 UMD：否

| 场景 | 是否阻塞 | 依据 |
|---|---|---|
| DDK2 render 路径（0x82:0xC/0x14） | 否 | IN 无 kicksync 字段；UMD 侧 allocator 用户态自理（r144/r199） |
| Legacy kick 路径（0x88:0x2/3/4） | 否 | `pvr_translate_kick` 只用 IN 参数做 sync-prim 翻译，不读 CCB |
| UMD CCB 生命周期（create/destroy） | 否 | r144 活体全 0 通过 |
| 未来：CCB-ring 直通模型 | （不适用） | 本桥未采用该模型；若采用则需重开本项 |

## 6. 标注

- **[实测]**：`pvr_cmd_kicksyncctx2_create/destroy/submit` 的行为（源码）；
  `0x82:0xC`/`0x82:0x14` IN 无 kicksync 字段（`mt_pvr_wire.h`）；
  r144 全生命周期活体通过；r56 rung6 无 PMR/heap 增发；
  r199 allocator 在 render context `+0x200`（用户态）；
  KMD 5.2.0 头的 IN 布局（8B vs 16B）。
- **[推断]**：Windows KMD server 侧分配 CCB 设备内存（r56 的解读；无源码，
  只有行为证据）；本桥直接包构造架构使该分配无消费者。
- **[待验证]**：无。本项关闭。若真实 UMD 某天表现出对 server 侧 CCB 的依赖
  （活体证据），重开。

## 门禁

- 本轮纯调研、无代码改动：`make -C mt-vgpu-guest check-offline` 全绿（见提交）。
- 引用路径核对：`kernel/recovery/mt_pvr_bridge.c`
  （2762/2787/2806/2605/2440）、`kernel/mt_pvr_wire.h`
  （323/362/426–460/556–602）、
  `reference/kmd-5.2.0-server-generated/common_musakicksync_bridge.h`
  （68–200）均存在。

## 证据

- `mt-vgpu-guest/reports/r392-evidence.txt`：关键源码摘录（handler 行为、
  wire 布局、KMD 头对比、r144/r56/r199 结论引用）。
