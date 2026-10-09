# r445：断电级冷重启后仍阻塞——确认为代码问题（参数组合非法 + reg890 状态机缺陷）

**结论：第 10 次断电级冷重启后，trial 依然无法重建。用户判断正确：这是代码的问题，不是硬件状态问题。两个独立的代码缺陷已定位。**

> Round r445 (2026-10-09). **断电级冷重启后阻塞确认 + 代码问题诊断。未执行活体，未修改代码。**

## 系统确认

| 检查项 | 结果 |
|---|---|
| 第 10 次冷重启（断电级） | ✅ uptime 0 min（19:57），关机几分钟后开机 |
| 模块状态 | ✅ mt_guest_probe/mt_pvr_bridge 均未加载 |
| PCI 设备 | ✅ 00:0e.0 [1ed5:0222] 可见，COMMAND=0x0003（MEMORY 启用） |
| 工作区 | ✅ 干净，HEAD 33478b7（r444，已 push） |

## 阻塞确认（[MEASURED]）

sudo insmod kernel/mt_guest_probe.ko enable_probe=1 query_info=1 probe_rpc=1 recover_channels=1
→ dmesg: probe with driver mt_guest_probe failed with error -16

-16（EBUSY）在断电级冷重启后依然存在。失败 probe 已安全 rmmod（refcount 0）。

## 代码问题诊断

### 缺陷 1：-EINVAL —— 非法参数组合（r443/r444 测试程序错误）

位置：kernel/mt_guest_probe.c:1258-1261

    if (recover_channels && (!query_info || !probe_rpc || trial_connect ||
            reserve_memory || test_memory_write ||
            prepare_resources || load_firmware || test_firmware_upload || runtime_context))
        return -EINVAL;

根因：recover_channels=1 与 reserve_memory=1 是互斥的。代码明确拒绝此组合。
r443/r444 的测试命令同时设置了这两个互斥标志，触发 -EINVAL。这是测试程序错误。

正确组合（reserve_memory 不带 recover_channels）：
    enable_probe=1 query_info=1 probe_rpc=1 reserve_memory=1

### 缺陷 2：-EBUSY —— recover_channels 路径不接受持久化的 reg890==2 状态

位置：kernel/mt_guest_probe.c:1333-1338

    u32 reg890 = readl(g->regs + 0x890);
    if ((reg890 != (recover_channels ? 1 : 0) &&
         !(trial_connect && !recover_channels && reg890 == 2)) ||
        readl(g->regs + 0x898) != 1) {
        ret = -EBUSY;
        goto unmap;
    }

根因：
- recover_channels=1 时期望 reg890 == 1
- 但固件 0x890 在 trial session 活跃时为 2
- 代码注释明确承认：firmware may boot with the session indicator set (cold boot does not clear it)
- 只有 trial_connect=1（且 !recover_channels）的路径接受 reg890==2
- recover_channels=1 的路径不接受 reg890==2，导致 -EBUSY

这是代码逻辑缺陷：既然冷重启不清除 0x890==2，recover_channels 应同样接受该状态。

### 影响链

缺陷 2 (-EBUSY) → recover_channels=1 无法 probe
→ 缺陷 1 (-EINVAL) → recover_channels + reserve_memory 组合被拒
→ trial 重建完全阻塞 → bridge -ENODEV → 活体无法执行

## 安全

- 未执行任何 live 操作；失败 probe 已安全 rmmod；未用 rmmod -f；未自行重启
- dmesg 零 WARN/BUG/Oops；本轮未修改任何代码（纯诊断）
- 证据：build/traces/r445/dmesg-r445.txt（0600）

## 门禁

- make -C mt-vgpu-guest check-offline：559 Python + 781 C 全绿
- make -C mt-vgpu-guest kernel W=1：零警告
- 无代码变更

## 诚实边界

- -EINVAL/-EBUSY 路径为静态分析 + dmesg 实证；0x890 实际值未直接读取
- reg890==2 持久化基于代码注释的明确陈述，非直接测量
- 本轮零硬件触碰（除安全 rmmod 外）；生产代码零变更

## 下一步（需用户决策）

1. P0：修复缺陷 2——recover_channels=1 路径应接受 reg890==2
2. P0：修正测试程序——reserve_memory 测试不应带 recover_channels=1
3. P1：验证修复后 reserve_memory=1（不带 recover_channels）是否成功
4. 阻塞解除前不再尝试活体
