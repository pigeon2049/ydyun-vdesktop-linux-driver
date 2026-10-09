# r448：补修复 mt_probe_channels 第 4 处 reg890==2（离线）

**结论：r447 发现的第 4 处遗漏已修复。`mt_probe_channels` 的 `recover_channels` 分支现接受 `reg890==2`。全面扫描确认无第 5 处。3 个新测试，反向验证精确 FAIL。纯离线，零硬件触碰。**

## 修复内容

`kernel/mt_guest_probe.c`（`mt_probe_channels`）：

```c
// 修复前：
if (readl(g->regs + 0x890) != 1 || readl(g->regs + 0x898) != 1) {
    ret = -EBUSY;
    goto release;
}

// 修复后：
/* 0x890==2 accepted (see mt_probe): firmware may boot with the
 * session indicator set; cold boot does not clear it. */
reg890 = readl(g->regs + 0x890);
if ((reg890 != 1 && reg890 != 2) || readl(g->regs + 0x898) != 1) {
    ret = -EBUSY;
    goto release;
}
```

- 新增 `u32 reg890` 局部变量；仿照 r446 line 401 的 `(reg890 != 0 && reg890 != 2)` 模式。
- `reg890==1` 正常路径不受影响；`0x898` 逻辑保持 `==1`（通道就绪位，无需例外）。

## 全面扫描（无第 5 处）

所有 `0x890` 读取点与 `-EBUSY` 返回点已核查：

| 位置 | 函数 | 状态 |
|---|---|---|
| :401 | `mt_reserve_memory` | r446 已修复 |
| :511 | `mt_snapshot_memory` | r446 已修复 |
| :644 | `mt_probe_channels` | **r448 本轮修复** |
| :1223 | `mt_probe` | r446 已修复 |
| :1340 | `mt_read_device_info` | r446 已修复 |
| :319/:330 | `mt_test_firmware_upload` | 不在 probe 关键路径（`test_firmware_upload` 参数门控）；`==0` 为破坏性测试前的安全期望，保留 |
| :364 | `mt_test_memory_write` | 同上（`test_memory_write` 参数门控），保留 |
| :814 | retained kick sysfs | 实验性 sysfs 路径，非 probe 关键路径；刻意窄范围，保留 |
| :1284 | `mt_probe` PCI 命令检查 | 非 reg890 检查，无关 |

**结论：recover_channels probe 路径的 4 处 reg890 检查已全部修复，无遗漏。**

## 新测试

`tests/guest/test_probe_890_recover.py` 新增 `TestProbeChannels890Acceptance`（3 tests）：

- `test_probe_channels_accepts_890_eq_2`：`(reg890 != 1 && reg890 != 2)` 模式存在
- `test_probe_channels_comment_mentions_890_eq_2`：注释记录 `0x890==2` 例外
- `test_probe_channels_old_strict_check_gone`：旧严格检查已替换

## 反向验证

- 新代码：10/10 通过（7 原有 + 3 新增）
- 回退 `mt_guest_probe.c`（stash 修复）：3/3 新增测试精确 FAIL
- 恢复后：10/10 通过

## 门禁

- `make -C mt-vgpu-guest check-offline`：**569 Python + 781 C 全绿**（1 skipped）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**

## 安全

- 纯离线轮；未触碰硬件，未加载模块；未自行重启。

## 诚实边界

- 修复基于静态分析 + r445/r447 dmesg 实证；`reg890` 实际值未在修复后实测。
- 活体验证待用户确认后（r449 trial 重建）。

## 下一步

- r449（待用户确认）：trial 重建 → `+0x120=0x1` 活体。
