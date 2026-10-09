# r444：Trial 在第 9 次冷重启后仍被阻塞——`reserve_memory` -EINVAL 持续，且出现新的持续性 -EBUSY

**结论：r444 活体未能执行。`mt_guest_probe` 在第 9 次冷重启后仍于 `reserve_memory=1` 报 -EINVAL（r443 阻塞点未解除）；更严重的是，无 `reserve_memory` 的 probe 也持续报 -16（EBUSY），与 r443（无 reserve_memory 成功）退化。按安全协议立即停止，未执行任何活体操作。**

> Round r444 (2026-10-09). **纯只读核查 + 阻塞确认，未活体。**

## 系统确认

| 检查项 | 结果 |
|---|---|
| 第 9 次冷重启 | ✅ uptime 1 min（19:41） |
| 模块状态 | ✅ `mt_guest_probe`/`mt_pvr_bridge` 均未加载 |
| 工作区 | ✅ 干净，HEAD `0b897ef`（r443，已 push） |

## 阻塞：probe 失败

### 1. `reserve_memory=1` 仍 -EINVAL（r443 阻塞点持续）

```
sudo insmod kernel/mt_guest_probe.ko enable_probe=1 query_info=1 probe_rpc=1 recover_channels=1 reserve_memory=1
→ EXIT:0（insmod 本身返回 0），dmesg：
mt_guest_probe 0000:00:0e.0: probe with driver mt_guest_probe failed with error -22
```

与 r443 完全一致：`reserve_memory` 是失败点。

### 2. 新退化：无 `reserve_memory` 也报 -16（EBUSY）

r443 的增量测试中 `enable_probe=1 query_info=1 probe_rpc=1 recover_channels=1`（无 reserve_memory）**成功**。
r444 同参数 probe 报：

```
mt_guest_probe 0000:00:0e.0: probe with driver mt_guest_probe failed with error -16
```

rmmod（refcount 0，安全）后重试一次，-16 **持续**（3 条 dmesg 记录：1×-22，3×-16）。

PCI 设备状态（`lspci` 层面正常）：
- `0000:00:0e.0 [1ed5:0222]` type 00 class 0x030000 PCIe Endpoint
- BAR 0/1/2 正常枚举；vgaarb：`VGA device added: decodes=io+mem, owns=io+mem`
- 无驱动绑定（`/sys/bus/pci/devices/0000:00:0e.0/driver` 不存在）

### 影响

`reserve_memory` → `prepare_resources` → `load_firmware` → `trial_connect` 链断裂，无 trial 即 bridge `0x82:0x12` -ENODEV（r436 教训）。**活体无法继续。**-16 的出现说明设备/PCI 层状态进一步恶化，非单纯 VRAM 分配问题。

## 安全

- 未执行任何 live 操作（未加载 bridge，未运行 `mt-ta-readback`，未构建双门控测试模块）
- 失败 probe 模块已 `rmmod`（refcount 0），系统恢复干净
- dmesg 零 WARN/BUG/Oops（仅 boot 期 CPU 通告与 apparmor 常规记录）
- 未使用 `rmmod -f`；未自行重启
- 证据：`build/traces/r444/dmesg-r444.txt`（0600）

## 门禁

- `make -C mt-vgpu-guest check-offline`：**559 Python + 781 C 全绿**（1 skipped，1 pre-existing ResourceWarning）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 无代码变更（本轮纯阻塞确认）

## 下一步（需用户/parent 决策）

1. **P0**：确认第 9 次是否为**完整关机再开机**（断电/长按电源），而非 warm reboot。r443/r444 的共同推测：冷重启未完全重置固件/BAR/VRAM 分配器状态。
2. **P1**：若确认为完整冷重启仍 -EINVAL/-16，需离线深挖 `mt_reserve_memory()`（`kernel/mt_guest_probe.c:390`）的 -EINVAL 路径与 -16（EBUSY）来源——vgaarb 持有？BAR2 映射冲突？固件握手状态机？
3. 在阻塞解除前，**不再尝试活体**（r380/r418/r421/r425/r432/r436/r440 教训延续）。

## Honest boundaries

- -22/-16 均为内核 probe 返回码实证；具体内部路径（`mt_vram_init`/`mt_vram_alloc`/vgaarb）未深挖，待 P1。
- "第 9 次冷重启"为用户陈述 + uptime 1min 佐证；是否为断电级冷重启 [UNCONFIRMED]。
- 本轮零硬件触碰（除安全 rmmod 外）；生产代码零变更；未链入下一轮。
