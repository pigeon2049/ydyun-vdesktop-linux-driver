# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](MEMORY-HISTORY-2026-10-07.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-07（r217 observer 分发活体验证，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r217：observer 分发活体验证，批准执行）

- r215 observer 分发在活体证明到达：新工具 `pvr_observe_ping` 发零填充 108B `0x82:0x14`（bogus context）回 `-ENOENT` 而非 `-ENOTTY`；control `0x82:0x1f` 仍 `-ENOTTY`。fresh file 即开即关，refs 1/0 不变，dmesg 零新增。工具零警告构建 + 5 项门禁（含反向）。`check-offline` 307 Python OK。详见 `reports/r217-observe-ping-live.md`。
- **会话未动，无需重载，freeze 继续。**
- 遗留：observer 全路径（合法上下文 + 真实窗口）仍无流量；GFX producer 仍 open。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r216：新构建上机 + L3，批准执行）

- 积压的 6 个本地提交已 push（`104fdeb..fff3769`）。r215 新构建（含 `0x82:0x14` observer）上机：装盘前验 strings + vermagic；单桥重载（probe 未碰），节点仍 `renderD128`；L3 全绿（node + smoke，refs 平衡），终态 ref 1/0，dmesg 零 WARNING/BUG/Oops。详见 `reports/r216-newbuild-reload.md`。
- observer 已在载但尚无真实流量（parked，不是 proven）；路由活体 ping 需新工具代码，留待下轮。**Freeze 已恢复。**
- 遗留：真实 GFX producer 仍 open；真实执行仍待 backend 接线。USB 短页标题日期问题留待对应轮。

---
