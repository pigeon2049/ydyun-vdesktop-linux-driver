# r447：r446 修复不完整——`mt_probe_channels` 第 4 处同样拒绝 reg890==2，trial 仍阻塞

**结论：r447 活体未能执行。r446 修复了 3 处 reg890==2 检查，但遗漏了 `mt_probe_channels`（第 4 处），`recover_channels=1` 仍在该处返回 -EBUSY。需 r448 补修复。**

## 系统确认

| 检查项 | 结果 |
|---|---|
| 第 11 次冷重启 | ✅ uptime 1 min（20:17） |
| 模块状态 | ✅ `mt_guest_probe`/`mt_pvr_bridge` 均未加载 |
| 工作区 | ✅ 干净，HEAD `40df15b`（r446，已 push） |
| 构建 | ✅ `make kernel` W=1 零警告（r446 修复已编译进 .ko） |

## 关键测试：r446 修复验证

**测试**：`sudo insmod kernel/mt_guest_probe.ko enable_probe=1 query_info=1 probe_rpc=1 recover_channels=1`
（**不带** `reserve_memory=1`，r446 修正后的合法组合）

**结果**：❌ probe 失败

dmesg：
```
mt_guest_probe 0000:00:0e.0: shared channel round-trip result=-16 mode=0 registered=0
mt_guest_probe 0000:00:0e.0: probe with driver mt_guest_probe failed with error -71
```

## 根因分析

### 进展：r446 修复在 `mt_probe` 生效

- r445：`mt_probe` 在 reg890 检查处返回 -16（EBUSY）
- r447：`mt_probe` 的 reg890 检查已通过（不再是 -16 的来源）
- **r446 修复在 `mt_probe`、`mt_read_device_info`、`mt_snapshot_memory` 3 处生效** ✅

### 新发现：第 4 处遗漏——`mt_probe_channels`

`kernel/mt_guest_probe.c:644`（`mt_probe_channels`，`recover_channels` 分支）：

```c
if (recover_channels) {
    if (readl(g->regs + 0x890) != 1 || readl(g->regs + 0x898) != 1) {
        ret = -EBUSY;
        goto release;
    }
```

- 该处**未包含** r446 的 `reg890==2` 例外
- `registered=0` 证实：在通道注册前即失败
- 返回 -16（EBUSY）→ 上层转为 -71（EPROTO）

### 完整清单

| 位置 | 函数 | r446 修复 | 状态 |
|---|---|---|---|
| ~1224 | `mt_probe` | ✅ | 生效 |
| ~1341 | `mt_read_device_info` | ✅ | 生效 |
| ~511 | `mt_snapshot_memory` | ✅ | 生效 |
| ~644 | `mt_probe_channels` | ❌ **遗漏** | **仍拒绝 reg890==2** |
| ~401 | `mt_reserve_memory` | （已有） | 接受 reg890==2 |

## 安全

- 未执行任何 live 操作（trial 未建立）
- 失败 probe 已 via `scripts/safe_rmmod.sh` 安全卸载（refcount 0）
- 未用 `rmmod -f`；未自行重启
- dmesg 零 WARN/BUG/Oops
- 证据：`build/traces/r447/dmesg-r447.txt`（0600）

## 门禁

- `make -C mt-vgpu-guest check-offline`：**566 Python + 781 C 全绿**（1 skipped）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 无代码变更（本轮纯验证）

## 诚实边界

- -16 来源为 `mt_probe_channels:644` 的静态分析 + dmesg `registered=0` 实证
- reg890 实际值未直接读取（但 r445/r446 逻辑链一致）
- 本轮零硬件触碰（除安全 rmmod 外）；生产代码零变更；**未链入下一轮**

## 下一步（r448 P0）

修复 `mt_probe_channels:644`：

```c
/* 0x890==2 accepted (see mt_probe; cold boot does not clear session indicator). */
u32 reg890 = readl(g->regs + 0x890);
if ((reg890 != 1 && reg890 != 2) || readl(g->regs + 0x898) != 1) {
    ret = -EBUSY;
    goto release;
}
```

+ 新增测试覆盖该路径 + 反向验证。
