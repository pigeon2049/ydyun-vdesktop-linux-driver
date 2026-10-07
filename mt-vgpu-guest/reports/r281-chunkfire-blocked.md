# r281：分块 fire 活体被持有挡回（批准执行，未触硬件）——holder 是会话桌面自身

- **结论**：r280 活体验证未执行：`renderD128` 被 PID 60651（`ai.opencode.desktop`，本机会话桌面自身）持有，bridge ref 1，`rmmod` 被拒（`in use`，rc=1）， meeting r120/r278 先例即停手。失败的 rmmod 不改变任何状态：refs 仍 1/1，本轮窗口 dmesg 仅起始 marker 一行、零新增 WARN/BUG/Oops（两条 WARNING 系 23:07/23:12 旧转储，r275 已知签名）。**Freeze 继续，会话未动。**

## 实测（执行过）

1. 只读预检：bridge ref 1、probe ref 1；`fuser` 指 PID 60651，comm 为会话桌面；`/` 27%；盘内 `.ko` 含 fire 代码（`fire seq` 3 处、`chunks=%u` 2 处），vermagic 对版 `6.12.111`。
2. `mkdir build/traces/r281`（空）+ kmsg 打 `[r281] chunkfire-live-start` 标记。
3. `sudo rmmod mt_pvr_bridge` → `in use` 被拒即停；未尝试 `-f`（红线禁止），未 insmod，未提交工作。
4. 恢复动作：无（无状态变化，无需恢复）；暂存区 `r281/` 为空。

## 边界

- holder 是被动 open（GPU 查询 fd，本窗口 dmesg 无其 bridge 命令流量），但 ref 非零即不可重载——被动不等于可卸。
- holder 是 agent 会话自身的桌面壳，agent 无法自行关闭（会断会话）；需用户侧释放（如关桌面 GPU 占用或另择窗口）。
- r280 代码仍在盘未上机；上机验证内容不变（`chunks=21` + `verified=1`）。

## 下一步（候选，需批准 + 需 holder 释放）

1. 用户释放 holder（ref 回 0）后重跑本计划：三开重载 + 真实 blit + 拆桥恢复 + L3。
