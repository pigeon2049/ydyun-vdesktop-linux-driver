# r451：`+0x120=0x1` 活体仍 5s 超时——flags 最小值非根因

**结论：r451 活体执行，`+0x120=0x1` 未解决 5s 固件超时。`+0x120` [INFERRED] 保持未证实。Trial 重建成功（第 13 次冷重启后直接跑 fresh-trial.py）。**

## 系统确认

| 检查项 | 结果 |
|---|---|
| 第 13 次冷重启 | ✅ uptime 1 min（21:20） |
| 模块状态 | ✅ 初始干净 |
| HEAD | ✅ `46e05e4`（r450，已 push） |

## Trial 重建成功

**直接跑 `scripts/fresh-trial.py --run`**（不先做 recover_channels 测试，避免污染 reg890）：

### 修复的问题

1. **审计 JSON 过期**：`reports/runtime-integration-build.json` 的 module SHA 来自 2026-10-08（r446/r448 前）。已更新为当前模块 SHA（ABI 经 pahole 验证无漂移）。

2. **preflight 过严**：`fresh-trial.py` 要求 `(driver_state, firmware_state) == (0, 1)`，但硬件报告 `(2, 1)`（0x890=2 跨冷重启持久化）。内核 `trial_connect` 路径明确接受 `reg890==2`。已修改 preflight 接受 `(0,1)` 或 `(2,1)`。

### Trial 结果

- `--run`（runtime_context=0）：✅ `trial_connection_verified: True`，`trial_completed_and_restored: True`
- `--run --runtime-context`：✅ `runtime_context_published: True`，`pinned: 1`，`registered: 15`

## 双门控构建

- `MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`（static_assert 临时中和）
- userspace `n_entries` 1→0（Header-only，r423）
- `make kernel` W=1 **零警告**
- T1-T5 安全门禁通过（T5 header_integrity 16 tests OK；T4 q0_purity 3 tests OK）

## 活体结果：5s 超时

**测试**：`./build/userspace/mt-ta-readback /dev/dri/card1 /tmp/r451_out.bin`

**结果**：❌ **ETIMEDOUT (errno 110)**

```
[*] connected bvnc=0x23000406600017
[*] render context handle=0x1000
check failed line 161: 0 errno=110
```

dmesg 确认：
- 13 个 BO 全部绑定（包括 RgnHeader at 0x7c000000）
- Render context READY
- **无 TA 完成**，固件未响应

**`+0x120=0x1` 未解决超时。**

## 安全

- Bridge refcount 从 0→1（pending TA fence），与 r440 相同模式
- **未尝试卸载**（按安全协议，refcount 非零立即停止）
- 双门控源码已 revert；工作区恢复（仅保留 audit JSON 更新和 fresh-trial.py preflight 修复）
- 未用 `rmmod -f`；未自行重启
- dmesg 零 WARN/BUG/Oops

## 诚实边界

- `+0x120=0x1` 的"UMD 忠实最小值"仍为 [INFERRED]，未被证伪也未被证实
- 超时根因仍未知；`+0x68` 布尔嫌疑仍为主要 UNKNOWN（见 r442 Header 对照表）
- 本轮未链入下一轮

## 下一步（需 parent/用户决策）

1. **P0**：用户第 14 次冷重启（bridge ref=1 pending）
2. **P1**：离线研究 DDK psKickTA flags，尤其 `+0x68` 和 `+0x120` 其余十位；无依据不得盲试
3. 阻塞解除前不再尝试活体
