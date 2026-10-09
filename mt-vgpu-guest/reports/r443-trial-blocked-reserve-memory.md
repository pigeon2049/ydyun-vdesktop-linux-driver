# r443：Trial 重建被 `reserve_memory` 阻塞——第 8 次冷重启后设备状态异常

**结论：r443 活体未能执行。`mt_guest_probe` 在 `reserve_memory=1` 时 probe 失败（-EINVAL），无法重建 trial。双门控构建与门禁均通过，但硬件状态阻止 live。**

## 系统确认

| 检查项 | 结果 |
|---|---|
| 第 8 次冷重启 | ✅ uptime 0 min（19:23） |
| 模块状态 | ✅ `mt_guest_probe`/`mt_pvr_bridge` 均未加载 |
| 工作区 | ✅ 干净，HEAD `25c0483`（r442，已 push） |

## 门禁与构建

- `make -C mt-vgpu-guest check-offline`：**559 Python + 781 C 全绿**（1 skipped）
- 双门控测试构建（`MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`，static_assert 中和，userspace `n_entries` 1→0）：`make kernel` W=1 **零警告**
- 构建后已 revert，工作区恢复干净

## 阻塞：Trial 重建失败

### 现象

`mt_guest_probe` insmod 参数递增测试：

| 参数组合 | 结果 |
|---|---|
| `enable_probe=1` | ✅ probe 成功 |
| `+ query_info=1` | ✅ 成功 |
| `+ probe_rpc=1` | ✅ 成功 |
| `+ recover_channels=1 reserve_memory=1` | ❌ probe 失败 **-EINVAL (-22)** |

`reserve_memory=1` 是失败点。dmesg 仅显示：
```
mt_guest_probe 0000:00:0e.0: probe with driver mt_guest_probe failed with error -22
```
无详细错误信息（4 次失败记录）。

### 影响

`reserve_memory` 是 `prepare_resources` → `load_firmware` → `trial_connect` → `runtime_context` 的前置依赖。无 `reserve_memory` 即无 trial，无 pinned trial 即 bridge `0x82:0x12` 返回 -ENODEV（r436 教训）。**活体无法继续。**

### 排查

- 双门控构建仅修改 `kernel/mt_ta_real.h`（defines）和 `userspace/mt-ta-readback.c`；probe 源码未动（`git diff --name-only` 确认）
- 旧版 probe .ko（r28-live）因内核版本不匹配无法加载（"Invalid module format"）
- `mt_reserve_memory()`（kernel/mt_guest_probe.c:390）可能返回 -EINVAL 的路径：
  - `mt_vram_init()` / `mt_vram_alloc()` 失败
  - 0x890 寄存器状态检查（`reg890 != 0 && reg890 != 2` → -EBUSY，非 -22）
- 设备 PCI 可见（00:0e.0 [1ed5:0222]），无驱动绑定

### 推测

第 8 次冷重启可能未完全重置固件/BAR 状态，或 VRAM 分配器遇到设备侧异常。需用户确认是否为完整关机再开机（非 warm reboot）。

## 安全

- 未执行任何 live 操作（未加载 bridge，未运行 mt-ta-readback）
-  dual-gate 源码已 revert；工作区干净
- 未使用 `rmmod -f`；未自行重启
- 证据：`build/traces/r443/dmesg-r443.txt`（0600）

## 门禁

- `check-offline`：559 Python + 781 C 全绿
- `make kernel` W=1：零警告（双门控 + revert 后）

## 诚实边界

- `reserve_memory` 失败根因未定位（需设备侧调试或确认冷重启完整性）
- `+0x120=0x1`（r442）尚未活体验收
- T2 像素回读仍 open；生产代码零变更
- **下一步**：用户确认第 8 次为完整冷重启（或执行第 9 次），后重开 trial 重建

## 机器状态

- 无模块加载（probe 已 rmmod）
- 在载无测试构建；源码为 committed 默认状态
- 两端 /tmp 零残留
