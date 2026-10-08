# r366: `submit_ta_work` 落地并通过活体验证 —— DM3/0x66 单 marker 经真实 op 提交，fence 语义全绿

## 结论

R2b 第一阶段完成：`submit_ta_work` 作为 `mt_marker_ops` 第 5 个独立 op 实现并合入，
经**真实 op**（`mt_bridge_submit_ta_work`，非一次性探针）活体验证：DM3 接受、
opcode `0x66`、完成码 `0x100`、fence signal、`check_fence` 四种语义（无依赖/
已完成/非法/真异步等待）全部通过。Freeze 完好（probe 未动），桥按计划重载一次。

## 实现落点

| 文件 | 改动 |
|---|---|
| `kernel/mt_marker_fence.h` | `struct mt_ta_work`（第 5 op 参数）；`mt_marker_ops.submit_ta_work`（+ABI 警告注释）；`mt_marker_fence.ta_params`（尾部追加）；`mt_fw_ta_marker_command()`、`mt_fw_event_matches_ta()`、`mt_ta_sync_update_apply()`、`mt_marker_ta_lookup_fence()`、`mt_marker_submit_ta_work()`、`mt_marker_complete_ta()`；`mt_marker_operations` 初始化补 `.submit_ta_work`；桥 export 声明 |
| `kernel/mt_ta_submit.h` | 新增 `MT_FW_TA_COMPLETE_CODE 0x100U`（r365 实测值入库） |
| `kernel/recovery/mt_pvr_bridge.c` | `mt_bridge_submit_ta_work()` + `EXPORT_SYMBOL_GPL`（op 在桥上下文运行，pending fence pin 桥而非验证模块） |
| `tests/test_ta_submit_op.py` | 门禁：pin 完成码 `0x100`（含反向验证） |

设计要点（r364 D2–D7）：
- **D2/D4**：DM=3、opcode `0x66`，包形沿用 r365 实测（`+0x0c=0x66`、`+0x48=wire_id`、`+0x4c=pid`，其余零）。
- **D5**：`kick_ta=1`→DM3；`kick_pr=1`→`-EOPNOTSUPP`（语义 TO-VALIDATE，不编造）；`kick_3d=0`→跳过（无 3D 侧逻辑）。
- **D6**：`check_fence` 输入等待（无 `s->lock` 下 `dma_fence_wait_timeout` 5s）→ `ta_upd_*` 写值（marker 级恒零，`mt_ta_sync_update_apply` 为 R2b 留实循环）→ `dma_fence_signal`。已分配但已完成 wire_id 视为满足（消除竞态），未知 id → `-EINVAL`。
- **D7**：硬件写前全校验；队列满 `-EAGAIN` 保留所有权；失败全回滚；wire_id 永不复用（含失败间隙）。
- **D8**：`vm->sealed` 校验门保持关闭（显式注释，R5 前置）；`ta_upd_count/ta_fence_count>0` → `-EOPNOTSUPP`（诚实拒绝，不伪造写入）；本轮 payload 限 marker 级。
- **锁契约**（与 `submit_tqx_work` 不同）：调用者**不得**持有 `s->lock`；op 内部按需短暂取锁。`check_fence` 等待时无锁（完成路径需锁）。
- **ABI 红线**：freeze 中 probe 的 `mt_marker_ops` 为 4 成员旧实例，**禁止**对其解引用第 5 成员（越界读）；验证模块只调桥 export。头文件与实现处均有显式警告。

## 活体验证（真机，2026-10-08）

验证模块 `mt_live_ta_work`（`build/traces/r366-live/`，未入库），经桥 export 调用真实 op：

| 测试 | 内容 | 结果 |
|---|---|---|
| T1 | DM3/0x66 单 marker，`check_fence=0` | wire=3，**完成码 `0x100`**，fence signaled ✅ |
| T2 | `check_fence`=<T1 已完成 wire> | 立即满足，wire=4 完成 ✅ |
| T3 | `check_fence`=0x7fffffff（非法） | `-EINVAL`，无提交 ✅ |
| T4 | `check_fence`=<pending 中 dm1 marker> | 真异步等待（probe drain 完成 dm1）→ TA wire=5，`0x100`，signaled ✅ |

首轮 T4 因测试模块漏置 `s->ready` 得 `-EHOSTDOWN`（op 行为正确，系测试 bug），修复后四项全绿，
`result=0`。验证后模块已立即卸载。

Live 边界（如实）：未提交真实 TA 渲染 payload（marker 级）；真异步 TA→TA 依赖等待未测
（待 probe 重建后）；`kick_pr=1` 语义仍 TO-VALIDATE。

## 门禁

- `make -C mt-vgpu-guest check-offline`：402 Python tests + 299 C checks，全绿。
- `make kernel W=1`：零警告（全模块重编，含新静态函数）。
- 新增 `tests/test_ta_submit_op.py`：pin `MT_FW_TA_COMPLETE_CODE==0x100`；
  **反向验证**：临时改为 `0x101` → 测试变红（257 != 256），恢复后变绿。
- `make probe` 未跑（WITH_BRIDGE 会 rmmod，与 freeze 协议冲突；沿用 r358/r360/r365 记法）。

## Freeze 与重载记录

- 基线：`mt_guest_probe` ref 1（00:0e.0），`mt_pvr_bridge` ref 0（r356 构建 `0d6bb8d7…`）。
- 本轮**一次计划内桥重载**（r360 流程）：holders 为空、refcnt=0 → `rmmod` →
  `insmod` 新桥（含 r366 op + r356 observer）→ `mt_bridge_submit_ta_work` 见于
  `/proc/kallsyms` → dmesg "unloaded cleanly"/"registered 'pvr' node" →
  `/dev/dri/{card0,card1,renderD128}` 正常。**probe 全程未碰**。
- 收尾：验证模块已卸载；`mt_guest_probe` ref 1、`mt_pvr_bridge` ref 0（r366 构建）；
  dmesg 无新增 WARN/BUG/Oops；marker 会话恢复空闲。

## 遗留与下一步

- R2b 后续：真实 TA payload（R5 sync-prim、`vm->sealed` 门开启）、probe 重建后
  `s->ops->submit_ta_work` 直调、TA→TA 异步等待。
- 本轮证据：`build/traces/r366-live/r366-live-dmesg.txt`（0600）；实现备份
  `build/traces/r366-impl/`。

---
*r366 · 2026-10-08 · one thing per round · commit 本地，不 push*
