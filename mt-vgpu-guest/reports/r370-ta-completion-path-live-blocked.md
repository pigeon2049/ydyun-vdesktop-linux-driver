# r370：生产 TA 完成路径已实现（桥侧轮询 + `mt_marker_complete_ta`）；活体验证被 EHOSTDOWN 阻塞（trial 状态问题，非本轮代码所致）

## 结论

1. **实现完成**：在 `mt_pvr_bridge.c` 新增生产 TA 完成路径——`0x82:0xC` TA dispatch 成功后，桥侧轮询 DM3 事件环等待 `0x100` 完成码（`pvr_ta_wait_complete`，2s 超时），命中后经 `mt_marker_complete_ta()` 退役 marker（fence signal）；超时则 `pvr_ta_abandon()` 以 `-ETIMEDOUT` error-signal fence 后清理，`check_fence` 等待者永不永久悬挂。**不再依赖 frozen probe** 的事件路径（其 `mt_marker_complete` 拒收 `0x100`，r368 已证）。
2. **门禁全绿**：`check-offline` 417 Python + 299 C 全绿；`make kernel` W=1 零警告；新增 `tests/test_ta_completion_path.py`（6 tests）+ 反向验证（删等待调用→红，恢复→绿）。
3. **一次计划内重载成功**（r360 流程）：rmmod 干净 → insmod 新桥 → refs probe=1/bridge=0 → dmesg 无 WARN/BUG/Oops → freeze 完好。
4. **活体验证被阻塞**：TA-only kick 的 dispatch 已到达（dmesg 见解码日志），但 `submit_ta_work` 返回 `-EHOSTDOWN`（112）——`mt_runtime_can_submit`（probe 内，frozen）拒绝。`pvr_session_acquire` 成功（trial.pinned/connected 为真），故卡点在其他条件（`r->published` / `r->event_result` / `service.running` / GPU `0x890` 寄存器 / `fw_state` / `trial_started` 之一）。**此为环境/trial 状态问题，非 r370 代码所致**（r367 同路径曾成功；r369 drain + 两次重载后状态变化）。

## 实现细节

**位置**：`mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.c`（桥侧，可重载；未碰 frozen probe）。

- `pvr_ta_wait_complete(g, s, wire_id)`（static）：调用者须持 `g->trial_lock`（== `s->lock`）。轮询 DM3 事件环（`s->queue` + `mt_fw_event_io_ops`，r366 验证者模式），匹配 `words[1]==MT_FW_TA_COMPLETE_CODE && words[2]==wire_id` 即调 `mt_marker_complete_ta()`；2s 超时返 `-ETIMEDOUT`。消费的事件留在环内——`s->count[DM3]` 归 0 后 probe 的 drain 将其无害暂存于 `t->events`（r368：无队列毒化）。
- `pvr_ta_abandon(g, s, wire_id)`（static）：超时清理。按 wire_id 找到 marker，从 pending 摘除，`dma_fence_set_error(-ETIMEDOUT)` + signal + put。杜绝 r368 wire 6 式永久悬挂。
- 接入点：`pvr_cmd_musakickgfx2()` 成功路径，`kfree(ctx)` 之后、`out.error=0` 之前。持 `trial_lock` 等待；超时/错误仅告警，不改变 ioctl 返回值（提交本身已成功）。

**为何不在 probe 实现**：`mt_runtime_event()` 为 probe 内 static 函数，已编译进 frozen 模块；`mt_marker_complete()` 为 header 内 `static inline`，probe 持有旧编译副本。桥侧轮询是唯一不碰 probe 的生产路径。

## 活体验证（阻塞）

**方法**：Python harness 经 `/dev/dri/renderD128`：`PVR_INIT(0x40046445, init_module=2)` 建连 → `PVR_BRIDGE(0xc0206440)` 发 `0x82:0xC`（`struct mt_pvr_cmd`：bridge_id=0x82/function_id=0xC，268B IN TA-only，12B OUT）。

**实测**（两次）：
- INIT ok。
- dispatch 到达：dmesg `musakickgfx2 dispatch: kick_ta=1 kick_3d=0 kick_pr=0 ...` ✅
- `submit_ta_work -> -112`（EHOSTDOWN）❌ —— 未到达 r370 新增的等待路径。

**EHOSTDOWN 排查**：
- `pvr_session_acquire` 成功 → `trial.pinned`/`connected` 为真。
- 卡点在 `mt_runtime_can_submit` 余下条件之一（见上）。用户态读 GPU BAR `0x890` 失败（I/O error，probe 独占映射），无法进一步定位。
- r367 同路径成功；差异在于 r369 drain + 两次桥重载。疑为 trial/GPU/firmware 状态需重建（如下一轮）。

## 诚实边界

- r370 新增代码**未被活体执行**（submit 在到达等待路径前即被拒）。实现正确性由编译 + 门禁 + 反向验证保证，端到端语义待 trial 恢复后验证。
- 超时/错误路径（`pvr_ta_abandon`）未活体触发。
- `mt_drain_pending.ko` 未使用（wire 6 已在 r369 清理）。

## 文件

- 实现：`mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.c`（+~110 行：两个 static helper + dispatch 接入）
- 门禁：`mt-vgpu-guest/tests/test_ta_completion_path.py`（6 tests）
- 报告：本文件 `mt-vgpu-guest/reports/r370-ta-completion-path-live-blocked.md`
- 证据（0600）：`mt-vgpu-guest/reports/r370-dmesg.txt`（dispatch 到达 + EHOSTDOWN 日志）
- 回滚件：`mt-vgpu-guest/build/traces/r370-recovery/mt_pvr_bridge.rollback-pre-r370.ko`（sha256 `8f669cfc…`，实为 r369 构建的新桥；另有 r367 构建 `1cc3d47f…` 可用）

## 门禁

- `make -C mt-vgpu-guest check-offline`：417 Python OK + 299 C OK ✅
- `make kernel` W=1：零警告 ✅
- 反向验证：删 `pvr_ta_wait_complete` 调用 → 1 failure（红）；恢复 → 全绿 ✅

## Freeze 状态

- `mt_guest_probe`：ref 1，全程未动 ✅
- `mt_pvr_bridge`：一次计划内重载（holders 空、refcnt 0 → rmmod cleanly → insmod → dmesg 确认注册 → /dev/dri 正常），现 ref 0 ✅
- dmesg：除本轮测试日志外，无新增 WARN/BUG/Oops ✅
- `make probe` 未跑（WITH_BRIDGE 会 rmmod），以 ioctl 交互为健康证据 ✅

## 建议的下一步

r371：trial/GPU 状态重建——定位 `mt_runtime_can_submit` 具体卡点（需内核态诊断），重建 trial 或 GPU/firmware 状态，使 `can_submit` 通过，然后重跑 r370 的活体验证（端到端：0x100 完成 + fence signal + 无悬挂）。
