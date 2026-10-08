# r364：TA firmware 提交通道设计（R4，离线设计）

**结论**：TA 提交通道设计为 `mt_marker_ops` 的第 5 个独立 op `submit_ta_work`（与 `submit_tqx_work` 并列、不碰 TQX 路径），TA 分配 DM3、firmware 命令 opcode 候选 `0x66`、`0x82:0xC` 的 268B IN 解码为 `struct mt_ta_submit_params`（104B，已钉入门禁）。DM 分配与 opcode 为推断、须活体验证；其余接口定义有实测支撑。本轮只做设计，未写实现逻辑。

## 1. 现状盘点（实测）

### 1.1 `submit_tqx_work` 模板（`kernel/mt_marker_fence.h:204`）

| 维度 | 实测内容 |
|---|---|
| 签名 | `submit_tqx_work(struct mt_marker_store *, struct mt_tqx_work *, struct dma_fence **)`，为 `mt_marker_ops` 4 个 op 之一 |
| 输入 | 已准备的 `mt_tqx_work`（`job.state == MT_JOB_HELD`，`context->route.type == 1`，`route.dm == 1`） |
| 校验 | profile `family==2`/`transfer_version==1`；`vm` sealed+uploaded；buffers 归属一致；`wire_id` 不溢出；`count[dm] < 63` |
| 机制 | kzalloc marker fence → `dma_fence_init` → `mt_work_job_move` → `packet+0x48` 打 wire_id 补丁 → `pending[dm=1]` 入队 → `mt_fw_queue_try_submit(queue, 1, 0, packet)` |
| 失败回滚 | 队列错误发生在硬件写之前：恢复 job 所有权、`packet+0x48` 清零、归还 pool_slices、`m->context=NULL` |
| fence | `mt_marker_complete` 在 firmware event（`words[2]==wire_id`）匹配时 `dma_fence_signal`；wire_id 永不复用 |

### 1.2 Firmware queue 机制（`kernel/mt_fw_queue_io.h`、`mt_fw_queue.h`）

- 6 个 DM × `0x2e30` 字节，命令 0x50 字节，per-DM producer spinlock；kick = `writel(dm, registers+0xb00)`。
- DM 分工（live 代码实证）：dm=1 TQX/transfer（`submit_tqx_work`），dm=2 3D Universal（`mt_live_3d.c` "to DM2"），dm=0 META/system（idle 检查排除 DM0）。
- 命令包格式（`kernel/mt_work_command.h`）：`+0x08` flags、`+0x0c` opcode、`+0x18` root_pa、`+0x20` process_id、`+0x28` command_va、`+0x30` bytes、`+0x48` fence/wire_id、`+0x4c` process_pid。
- Opcode 表：`0x64` marker/default、`0x65` preempt、`0x66` RGXVertex/UniversalQueue、`0x67` Transfer、`0x68` RGXCompute、`0x69` CopyEngine。

### 1.3 `0x82:0xC` IN 语义（r363 活体实测，268B）

`kick_ta=1`、`kick_pr=1`、`kick_3d=0`、`abort=0`；`ta_cmd_size=360`、`cmd_3d_size=544`、`cmd_3dpr_size=544`；`client_ta_upd_count=1`（余 0）、`pr_fence_value=1`；`p_ta_cmd` 堆地址非零、`p_client_ta_upd_*` 栈数组非零、`h_pr_fence_ufo_block=0x102d`；`num_draw_calls/num_indices/num_mrts=0`（最小 TA kick，无实际绘制）。45 字段全表见 `reports/r363-field-table.txt`，wire 结构见 `kernel/mt_pvr_wire.h`（r356 入库）。

### 1.4 r208 per-file VM/BO 边界（实测）

- `mt_bo_system_borrow()` 可把 PVR 页借入设备 BO store（`mt_gpu_vm_bind_many()` 接受）；但当前 PVR per-file VM 的页表 BO/PMR facade 是 `store=file` 的 CPU-only 规划域，不可进入设备 session。
- 真实路径需要：per-file、设备 buffer store 支持、已上传的 GPU VM；每个 render context 的 execution process/context；PMR borrowed BO 按 per-file reservation VA 绑定；CCB 资源闭包；嵌套 sync/PMR 数组校验。

## 2. 设计决策

### D1：独立 op `submit_ta_work`（推断）

`mt_marker_ops` 新增第 5 个 op，与 `submit_tqx_work` 并列：

```c
int (*submit_ta_work)(struct mt_marker_store *,
                      struct mt_ta_work *,
                      struct dma_fence **);
```

不复用/不分支进 `submit_tqx_work`：TA 在 DM、work 结构、校验（`route.dm`）、命令构造上全部不同；在已验证的 TQX 路径里加分支会污染它。`struct mt_ta_work` 镜像 `mt_tqx_work`（`job` + `context` + `pool_slices[3]`，另加 `struct mt_ta_submit_params params`）。

### D2：TA 分配 DM3（推断→待活体验证）

dm=1（TQX）、dm=2（3D）已被 live 代码占用，dm=0 为 META/system。TA 是独立引擎，取 dm=3。marker 框架的 idle/disconnect 循环本就遍历 dm 1..5，结构上直接支持。若 firmware 拒收，回退探测 dm 4/5。**本轮不做断言**，门禁只钉"不与 0/1/2 碰撞"。

### D3：`struct mt_ta_submit_params`（104B，布局实测钉死）

`0x82:0xC` IN 中提交路径需要的解码子集（字段来源：r363 45 字段表 ↔ `mt_pvr_musakickgfx2_in`）：

| 成员 | 来源 IN 字段 | 说明 |
|---|---|---|
| `ta_cmd_va` (u64) | `p_ta_cmd` | TA 命令缓冲，firmware 可见 VA（D8 要求进 per-file VM） |
| `ta_cmd_size` (u32) | `ta_cmd_size` | r363 实测 360 |
| `kick_flags` (u32) | `bbKickTA`/`bbKickPR` | `MT_TA_KICK_TA/BIT0`、`MT_TA_KICK_PR/BIT1` |
| `ta_upd_sync_off/val/block` (u64×3) | `p_client_ta_upd_*` | sync prim update 数组（r159 语义） |
| `ta_upd_count` (u32) | `client_ta_upd_count` | r363 实测 1 |
| `ta_fence_sync_off/val/block` (u64×3) | `p_client_ta_fence_*` | TA fence 数组 |
| `ta_fence_count` (u32) | `client_ta_fence_count` | r363 实测 0 |
| `pr_fence_block` (u64) | `h_pr_fence_ufo_block` | r363 实测 0x102d |
| `pr_fence_offset/value` (u32×2) | `pr_fence_ufo_sync_offset/value` | r363 实测 0/1 |
| `check_fence` (s32) | `check_fence` | 输入依赖 fence |

Userspace 指针一律在 decode 时捕获，submit 路径只见内核侧拷贝（D8）。

### D4：Firmware 命令 opcode 候选 `0x66`（推断→待活体验证）

`0x66` = RGXVertex/UniversalQueue（`mt_work_opcode()` type 3/11）；TA 消费顶点/分块工作。若 firmware NAK，后续轮从 Windows KMD 抓 TA opcode。**本轮不做断言**。

### D5：`0x82:0xC` 字段 → firmware 映射

| IN 条件 | 提交动作 |
|---|---|
| `kick_ta=1` | 向 DM3 提交 TA 命令包（`ta_cmd_va`/`ta_cmd_size`，opcode `0x66` 候选） |
| `kick_pr=1` | PR 语义待定：独立小包或合并包，**标记 TO-VALIDATE**（r363 仅观测到开关为 1，无执行证据） |
| `kick_3d=0` | 跳过 3D 提交 |
| `abort=1` | 不提交，直接返回（与现有 abort 语义一致） |
| `ta_upd_count>0` | 完成时按数组写 sync prim 值（D6） |
| `check_fence != 0` | 提交前等待对应 dma_fence（D6） |
| OUT `update_fence` | ← 本次 marker fence 的 wire_id |

### D6：Fence/sync 语义（实测框架 + 推断组合）

- **输入依赖**：`check_fence`（MTGPU_FENCE int32）解析为 dma_fence，提交前等待——沿用框架既有 fence 等待路径（实测存在）。
- **TA update 写回**：firmware 完成事件到达 → `mt_marker_complete` 匹配 wire_id → 按 `ta_upd_*` 数组写值（r159 离线确定的 update 语义，活体在 r223 验证过写回机制）→ `dma_fence_signal`（实测框架）。
- **输出**：OUT.`update_fence` = wire_id；UMD 侧 `SyncPrimWait` 等待之（r296 实测其语义）。

### D7：错误处理与回滚（照抄 `submit_tqx_work` 实测模式）

1. 所有校验在硬件写之前；任一失败直接返回错误码，不碰队列。
2. 队列满 → `-EAGAIN`，job 所有权保留，可重试/取消（`mt_work_job_move` 回移）。
3. `try_submit` 失败 → 恢复：`packet+0x48` 清零、job 回移、pool_slices 归还、`m->context=NULL`、fence put。
4. wire_id 永不复用（含失败间隙），防止延迟完成误匹配（框架已有注释）。
5. **红线**：不得用 accept-and-log 代替执行——op 要么真实提交，要么返回明确错误；observer 式的"看但不执行"只允许出现在 bridge 侧观察口（如 r356），不允许出现在执行路径。

### D8：内存对象（r208 实测边界）

- TA 命令缓冲：`p_ta_cmd` 指向的 UMD 堆页 → 经 `mt_bo_system_borrow()` 借入设备 BO store → 绑定进 per-file GPU VM（r208：当前 `store=file` facade 不可用，必须走设备 store + 真实 VM + 上传页表）。
- Sync prim 数组：IN 中的栈数组在 decode 时拷贝进内核（小尺寸），不直接引用 userspace。
- CCB 资源闭包：r208 要求"CCB 所引用 PMR 在该 VM 的绑定和存活引用"——TA 的 CCB 语义（r157–r161 系 TDM 语义，不可直接套用）**留待 R2b 设计轮细化**，本轮明确标出为依赖项。
- Per-file VM bring-up 本身是 R5（r208），与 R4 并行推进；`submit_ta_work` 的校验门（`vm->sealed && vm->uploaded`，照抄 TQX）在 R5 落定前保持关闭。

## 3. 待验证清单（TO-VALIDATE，须活体）

| # | 未知项 | 验证方式 |
|---|---|---|
| V1 | DM3 分配是否被 firmware 接受 | 活体单发 TA marker 包，观察 firmware 事件/错误 |
| V2 | opcode `0x66` 是否为 TA 命令码 | 同上；NAK 则抓 Windows KMD 的 TA opcode |
| V3 | `kick_pr=1` 的语义（独立提交 vs 合并） | 需真实 PR 工作负载观测 |
| V4 | TA 命令缓冲的 firmware 解析格式 | R2b：`p_ta_cmd` 内容的 firmware 语义 |
| V5 | per-file VM bring-up（R5）完成后 `vm->sealed` 门才能开 | r208 后续轮 |
| V6 | TA DM 的完成事件格式（`words[2]==wire_id` 是否同样适用） | 活体首发后看 event |

## 4. 与 R2b 的关系 / 下一步

本轮（R4）只解决"通道存在性"：op 接口、DM、包格式、fence 框架。R2b（`0x82:0xC` 真实执行）在此基础上做：
1. V1–V6 的活体验收（首个 TA marker 活体轮，单发、不提交真实渲染）。
2. TA 命令缓冲的 firmware 语义（V4）+ CCB 资源闭包（D8 依赖项）。
3. `kick_pr` 语义（V3）。

## 5. 交付物与门禁

- 接口头文件：`kernel/mt_ta_submit.h`（本轮新增；只含接口定义与静态断言，**无实现逻辑**）。
- 门禁：`tests/test_ta_submit_layout.py` 钉 `mt_ta_submit_params` 尺寸（104B）/关键偏移/DM 常量/不与 live DM 碰撞；反向验证（改错尺寸即红）已做；`make -C mt-vgpu-guest check-offline` 全绿；`make kernel` W=1 零警告（含新头文件编译）。
- 全程离线：未加载模块、未提交 GPU 工作、未碰 freeze 会话（bridge ref 0 / probe ref 1 不变），dmesg 未动。

## 6. 实测与边界（如实记录）

1. 本轮所有"实测"指对仓库代码/语料/既往轮次报告的只读核对；TA 通道本身尚未实现，更未上真机——D2/D4/V1–V6 均为待验证，不做断言。
2. `kick_pr` 语义：r363 只观测到开关为 1，无任何执行证据；本轮不编造语义，标 TO-VALIDATE。
3. TA 命令缓冲内容语义（V4）超出本轮范围；`0x36ec0` 的 XMM 打包逻辑（r361）只解决 IN 构造，不涉及 firmware 包格式。
