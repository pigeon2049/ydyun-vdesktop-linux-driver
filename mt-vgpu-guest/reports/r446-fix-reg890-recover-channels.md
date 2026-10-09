# r446：修复 reg890 状态机缺陷——`recover_channels` 路径接受 `reg890==2`

**结论：r445 定位的两个缺陷已修复。缺陷 2（代码逻辑缺陷）：`mt_guest_probe.c` 三处 `reg890` 检查现在接受 `recover_channels=1` + `reg890==2`。缺陷 1（测试程序错误）：新增门禁锁定非法参数组合的 -EINVAL 拒绝。纯离线轮，零硬件触碰。**

## 修复内容

### 缺陷 2：`recover_channels` 路径接受 `reg890==2`

**根因**（r445）：固件可能以 session indicator 置位启动（`reg890==2`），冷重启不清除。
只有 `trial_connect` 路径接受 `reg890==2`，`recover_channels` 路径不接受 → `-EBUSY`。

**修复**（`kernel/mt_guest_probe.c`，3 处）：

1. **`mt_probe`**（主修复）：
   ```c
   // 旧：
   !(trial_connect && !recover_channels && reg890 == 2)
   // 新：
   !(reg890 == 2 && (trial_connect || recover_channels))
   ```
   注释同步更新：`accepted when trial_connect or recover_channels`。

2. **`mt_read_device_info`**（一致性）：
   同样的 `reg890 == 2 && (trial_connect || recover_channels)` 异常。

3. **`mt_snapshot_memory`**（一致性）：
   ```c
   // 旧：
   if (readl(g->regs + 0x890) != (recover_channels ? 1 : 0) || ...
   // 新：
   u32 reg890_snap = readl(g->regs + 0x890);
   if ((reg890_snap != (recover_channels ? 1 : 0) && reg890_snap != 2) || ...
   ```
   注：此处 `recover_channels==0`（非法组合在 `mt_probe` 已被拒绝），
   但为与入口检查（已接受 0/2）一致，同样接受 2。

**安全性**：`reg890==1` 的正常路径不受影响（`reg890 != (recover_channels ? 1 : 0)`
仍然是主条件）；`reg890` 的其他值（0/3/4...）仍然被拒绝。

### 缺陷 1：测试程序修正 + 门禁

r443/r444 手动测试用了非法组合 `recover_channels=1` + `reserve_memory=1`
（代码在 `mt_probe:1258-1261` 明确拒绝，返回 `-EINVAL`）。

- `scripts/fresh-trial.py` 已正确（`reserve_memory=1` 不带 `recover_channels=1`），无需修改。
- 新增门禁锁定该拒绝逻辑（见下）。

## 新增测试和门禁

**新文件**：`tests/guest/test_probe_890_recover.py`（7 tests）：

| 测试类 | 测试 | 覆盖 |
|---|---|---|
| `TestRecoverChannels890Acceptance` | `test_mt_probe_accepts_890_eq_2_on_recover` | mt_probe 接受 reg890==2 |
| | `test_mt_probe_comment_mentions_recover_channels` | 注释文档同步 |
| | `test_read_device_info_accepts_890_eq_2_on_recover` | 两处修复一致性 |
| | `test_snapshot_memory_accepts_890_eq_2` | 第三处修复一致性 |
| | `test_old_trial_connect_only_pattern_gone` | 旧模式已替换 |
| `TestIllegalParamComboRejected` | `test_recover_plus_reserve_memory_rejected` | 非法组合 → -EINVAL |
| | `test_recover_plus_trial_connect_rejected` | 非法组合 → -EINVAL |

### 反向验证

- 新代码：7/7 通过。
- 回退到 r445 代码（`git stash`）：5/7 失败（5 个缺陷 2 测试精确 FAIL；
  2 个缺陷 1 测试通过，符合预期——拒绝逻辑在旧代码中已存在）。
- 恢复修复后：7/7 通过。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**566 Python + 781 C 全绿**
  （559+7 新，1 skipped，1 pre-existing ResourceWarning）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 反向验证通过（见上）

## 安全

- 纯离线轮：未加载模块，未触碰硬件，未执行活体。
- 用户确认无误后由用户重启（r447 待办）。

## 诚实边界

- 修复基于静态分析 + r445 的 dmesg 实证；`reg890` 实际值未在修复后实测。
- `reg890==2` 的语义（"trial session active"）来自代码注释，非直接测量。
- 活体验证待用户重启后的 r447。

## 下一步

- **r447**（待用户重启确认后）：trial 重建 → `+0x120=0x1` 活体。
- 若 trial 仍阻塞：继续深挖（但 r446 修复了已知的两个阻塞点）。
