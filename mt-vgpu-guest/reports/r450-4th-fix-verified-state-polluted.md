# r450：4 处修复验证成功，但验证测试污染硬件状态——需第 13 次冷重启或代码修复

**结论：r446/r448 的 4 处修复验证成功（`recover_channels=1` 在 `reg890==2` 时不再 -EBUSY）。但验证测试将硬件 `reg890` 从 2 改为 1，导致 `mt_reserve_memory`（trial 构建必需）返回 -EBUSY。活体被阻塞，需用户决策：13 次冷重启，或修复 `mt_reserve_memory` 接受 `reg890==1`。**

## 系统确认

| 检查项 | 结果 |
|---|---|
| 第 12 次冷重启 | ✅ uptime 0 min（20:42） |
| 模块状态 | ✅ 初始干净，无 mt 模块加载 |
| HEAD | ✅ `54f2a7f`（r449，已 push） |
| PCI 设备 | ✅ `00:0e.0` MTT S3000 [1ed5:0222]，无驱动绑定 |

## 关键验证：4 处修复生效 ✅

**测试**：`sudo insmod kernel/mt_guest_probe.ko enable_probe=1 query_info=1 probe_rpc=1 recover_channels=1`

**结果**：✅ **成功**（EXIT:0）

```
mt_guest_probe 0000:00:0e.0: orphan channel 3 withdrawal response=0x0
mt_guest_probe 0000:00:0e.0: orphan channel 2 withdrawal response=0x0
mt_guest_probe 0000:00:0e.0: orphan channel 1 withdrawal response=0x0
mt_guest_probe 0000:00:0e.0: orphan channel 0 withdrawal response=0x0
mt_guest_probe 0000:00:0e.0: shared channel round-trip result=0 mode=1 registered=15
mt_guest_probe 0000:00:0e.0: info response: magic=aa557491 version=2 osid=6; rendering unavailable
mt_guest_probe 0000:00:0e.0: experimental Guest transport bound; trial_bus_master=0; no render node
```

**对比 r447**（修复前）：
- r447：`shared channel round-trip result=-16 mode=0 registered=0` → probe failed -71
- r450：`shared channel round-trip result=0 mode=1 registered=15` → **成功**

**r446/r448 修复验证通过！**

## 阻塞：验证测试污染硬件状态

**现象**：`reserve_memory=1`（trial 构建必需）返回 -16（EBUSY）

**根因**：
1. `recover_channels=1` 成功后，硬件寄存器 `reg890` 从 2 变为 1
2. `mt_reserve_memory`（kernel/mt_guest_probe.c:401）只接受 `reg890==0` 或 `reg890==2`：
   ```c
   if ((reg890 != 0 && reg890 != 2) || readl(g->regs + 0x898) != 1)
       return -EBUSY;
   ```
3. `reg890` 是硬件寄存器，`rmmod` 不重置，跨模块加载持久化

**状态机**：
- 冷启动：`reg890==2`（固件带 stale session indicator）
- `recover_channels=1` 成功后：`reg890==1`
- `mt_reserve_memory` 接受：`0` 或 `2`，**不接受 `1`**

**影响**：`fresh-trial.py`（trial 构建脚本）使用 `reserve_memory=1`（不带 `recover_channels`），在 `reg890==1` 时失败。**活体无法继续。**

## 安全

- 验证测试后已 `rmmod`（refcount 0），系统恢复干净（无模块加载）
- 未使用 `rmmod -f`；未自行重启
- dmesg 零 WARN/BUG/Oops
- 未执行任何 live 操作（trial 未建立）

## 诚实边界

- 4 处修复验证为 [MEASURED]（dmesg 实证）
- `reg890` 从 2→1 为推断（基于代码逻辑 + 行为变化），未直接读取寄存器值
- `mt_reserve_memory` 不接受 `reg890==1` 为静态代码分析 + 实证

## 下一步（需用户/parent 决策）

**选项 A（推荐）**：第 13 次冷重启
- 重置 `reg890` 为 2
- 然后直接运行 `fresh-trial.py` 构建 trial（不先做 `recover_channels` 测试）
- 进行 `+0x120=0x1` 活体

**选项 B**：代码修复（r451 离线轮）
- 修改 `mt_reserve_memory` 接受 `reg890==1`：
  ```c
  if ((reg890 != 0 && reg890 != 1 && reg890 != 2) || ...)
  ```
- 增加测试，反向验证
- 然后无需重启，直接 trial 构建

**建议**：选项 A（冷重启）更稳妥，避免在硬件状态已污染的情况下修改代码。选项 B 更彻底，解决状态机不完整问题。
