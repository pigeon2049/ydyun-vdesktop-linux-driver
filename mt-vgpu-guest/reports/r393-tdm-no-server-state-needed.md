# r393：R6-6 调研结论——server 侧 TDM context 不需要真实化，`0x89:0x8` 空 token 已足够（离线，零硬件触碰）

## 结论

**R6-6（TDM context server 侧真实化）评估完成：不需要实现。** `0x89:0x8`
（`RGXTDMCreateTransferContext2`）当前的空 token 实现（mint
`MT_PVR_KIND_TDM_CONTEXT`，存 2 个 u64 arg）对本桥架构已足够。r150 活体已证明
真实 UMD 的 TDM 全生命周期（`0x89:0x8` create → `0x89:0xa` submit →
`0x89:0x9` destroy）在空 token 下全部返回 0、UMD 正常推进到 submit 并产生
可观测的 CCB 字节（r174）。本桥的 DDK2 路径采用**直接固件包构造**
（r366/r367 TA、r382 3D），TDM submit（`0x89:0xa`）当前为 accept-and-log
（r174），没有任何执行路径消费 TDM context 的 server 侧状态。

**工作项状态**：R6-6 关闭为 **won't-do by design**（有据可查，见下）。
若未来实现真实 TDM 执行（firmware transfer 提交），可重开。

## 1. TDM 的准确定义

**TDM = Transfer Data Manager**（KMD 生成头 `common_musaxfer_bridge.h`：
"MUSAXFER" = MUSA Transfer）。它是 PowerVR/RGX 的 **2D/blit 引擎**，
与 3D 管线（TA/3D，DM2/DM3）独立，负责 2D 操作（blit、fill、copy）。
DDK2 变体命令（KMD 5.2.0）：

| 命令 | 功能号 | IN/OUT | 说明 |
|---|---|---|---|
| `MUSACreateTransferContext2` | 0x8 | 12B/12B | 创建 transfer context |
| `MUSADestroyTransferContext2` | 0x9 | 12B/4B | 销毁 |
| `MUSASubmitTransfer3` | 0xa | 108B/4B | 提交 transfer（r174 accept-and-log） |
| `MUSAGetSharedMemory` | 0x5 | 0B/20B | 取 2D 共享内存（已真实实现） |
| `MUSAReleaseSharedMemory` | 0x6 | 8B/4B | 释放 |

## 2. Linux 侧现状（实测：读源码）

`kernel/recovery/mt_pvr_bridge.c`：

- `pvr_cmd_tdm_context2_create`（:3484）：`pvr_in` 12B →
  `pvr_object_new(file, MT_PVR_KIND_TDM_CONTEXT)` → 存
  `arg0=device_mem_context`、`arg1=context_type` → OUT 12B
  `{transfer_context, error}`。**无 firmware context 分配。**
- `pvr_cmd_tdm_context2_destroy`（:3504）：`pvr_object_find` 存在性校验 →
  `list_del` + `kfree`。生命周期 bookkeeping 完整。
- `pvr_cmd_tdm_submit3_observe`（:3686，0x89:0xa，r174）：遍历
  `file->objects` **仅做存在性校验**（`have_ctx`），随后报告 CCB 窗口
  digest，返回 0。**不读任何 context 状态，不执行。**
- `pvr_cmd_tdm_shmem`（:3442，0x89:0x5）：**真实实现**——分配两个独立
  PMR（CLI + USC，各 0x2000B），r150 活体验证 UMD import/unref/release
  全生命周期正常。

即：TDM context 对象在本桥中是纯 bookkeeping token；唯一真实的 TDM
资源是 shared-memory PMR（已实现）。

## 3. 是否需要真实化：不需要（有据）

**[实测]** r150（活体）：真实 UMD（SHA-256 `b3058c02…`）调用
`0x89:0x8` create → ret=0 → 后续流程继续 → `0x89:0x9` destroy → ret=0。
**空 token 未阻塞 UMD**；UMD 正常推进到 `0x89:0xa` submit 阶段。
当时的阻塞点是 `0x1:0xc` 的 `num_cores=0`（已修复），与 TDM context 无关。

**[实测]** r174（活体）：`0x89:0xa` accept-and-log 从真实 UMD 捕获到
非零 CCB 字节（`r174-real-ccb.jsonl`）。这证明 UMD 带着空 token 的
TDM context 走完了 submit 全流程——context 的空 token 对 UMD 透明。

**[实测]** r112：legacy-TDM（`0x89:0x0`，`+0x38` 系）可能是 vendor 死代码
（五种堆条件下 `+0x54` 恒零，UMD 自己都不走它）；真 2D 路径走新 DDK
分支（`+0x228` 系，即 `0x89:0x8`）。本轮结论仅针对 DDK2 `0x89:0x8`。

**[架构]** 本桥 DDK2 路径（r366/r367/r382）：内核直接构造 firmware
工作包经 DM 提交。TDM submit 当前为 accept-and-log（r174），**没有任何
代码路径读取 TDM context 的 server 侧字段**（`arg0`/`arg1` 写入后从未被读）。
若未来实现真实 TDM 执行，其 context 设计可参照 R6（render context）
模式另立轮次；当前 submit 模型下无消费者。

**[推断]** Windows KMD 可能在 server 侧为 transfer context 分配了设备
内存（如 r56 对 CCB 的解读），但那是它的实现内务；Linux 桥只要 UMD
行为一致（r150/r174 已证），就不必复制该内务——与 r392 的 CCB 结论同构。

## 4. 与 R6 的关系：独立

- R6（render context，`0x82:0x12`）：11 BO + CSW + firmware 执行上下文，
  被 `0x82:0xC`/`0x82:0x14` 经 `h_render_context` 消费（r389–r391 真实化）。
- R6-6（TDM context，`0x89:0x8`）：仅被 `0x89:0xa` submit 做存在性校验；
  TDM shared-memory PMR（`0x89:0x5`）独立已真实。

两者无依赖、无共享状态。R6-6 的结论不影响 R6-1~R6-4。

## 5. 是否阻塞真实 UMD：否

| 场景 | 是否阻塞 | 依据 |
|---|---|---|
| UMD TDM 初始化（`0x89:0x5`→`0x89:0x8`） | 否 | r150 活体：shmem 真实 PMR + context 空 token，UMD 全过 |
| UMD TDM 提交（`0x89:0xa`） | 否 | r174 活体：accept-and-log 捕获真实 CCB，UMD 不报错 |
| UMD TDM 销毁（`0x89:0x9`） | 否 | r150 活体：destroy ret=0，无泄漏 |
| 未来：真实 TDM 执行 | （不适用） | submit 当前为 observer 模型；若实现真实执行则另立项 |

## 6. 标注

- **[实测]**：`pvr_cmd_tdm_context2_create/destroy` 的行为（源码 :3484/:3504）；
  `pvr_cmd_tdm_submit3_observe` 仅存在性校验（源码 :3686–3710）；
  `pvr_cmd_tdm_shmem` 真实 PMR（源码 :3442）；
  r150 的 `0x89:0x8`/`0x89:0x9` 全 0 通过；r174 的 CCB 捕获；
  KMD 5.2.0 `common_musaxfer_bridge.h` 的命令定义（0x8/0x9/0xa）。
- **[推断]**：Windows KMD server 侧 TDM 分配（无源码，只有 r56 类比）；
  TDM = 2D/blit 引擎独立于 3D（KMD 头命名 + r33 包格式证据）。
- **[待验证]**：无。本项关闭。若真实 UMD 某天表现出对 server 侧 TDM
  context 的依赖（活体证据），或实现真实 TDM 执行时，重开。

## 门禁

- 本轮纯调研、无代码改动：`make -C mt-vgpu-guest check-offline` 全绿（见提交）。
- 引用路径核对：`kernel/recovery/mt_pvr_bridge.c`
  （3484/3504/3686/3442）、`kernel/mt_pvr_wire.h`
  （127–211/522–526）、
  `reference/kmd-5.2.0-server-generated/common_musaxfer_bridge.h`
  （命令 0x8/0x9/0xa 定义）均存在。

## 证据

- `mt-vgpu-guest/reports/r393-evidence.txt`：关键源码摘录（handler 行为、
  wire 布局、KMD 头对比、r150/r174/r112 结论引用）。
