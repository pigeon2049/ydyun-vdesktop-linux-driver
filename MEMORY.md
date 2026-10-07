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

最后更新：2026-10-07（r218 observer 全路径活体验证，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r218：observer 全路径活体验证，批准执行）

- observer 全路径首次在活体走通：`pvr_observe_ping` 扩展为 11 步（ping/control/envelope 四建/fire/teardown 五项），11/11 ok exit 0；桥 dmesg 行标量全上报（check=1 update=1，零窗口零统计）。阵列全 NULL（门禁级不解引用），只证明全链不证明数组语义。详见 `reports/r218-observe-fullpath-live.md`。
- 门禁 5→8 项（含一次无效反向后的精确反向验证）；`check-offline` 310 Python OK。refs 1/0 不变，dmesg 零新增。**无重载，freeze 继续。**
- 遗留：真实数组流量仍待 producer；真实执行仍待 backend 接线。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r217：observer 分发活体验证，批准执行）

- r215 observer 分发在活体证明到达：新工具 `pvr_observe_ping` 发零填充 108B `0x82:0x14`（bogus context）回 `-ENOENT` 而非 `-ENOTTY`；control `0x82:0x1f` 仍 `-ENOTTY`。fresh file 即开即关，refs 1/0 不变，dmesg 零新增。工具零警告构建 + 5 项门禁（含反向）。`check-offline` 307 Python OK。详见 `reports/r217-observe-ping-live.md`。
- **会话未动，无需重载，freeze 继续。**
- 遗留（r218 已推进：合法 envelope 全路径走通，阵列仍 NULL）：真实数组流量仍待 producer。USB 短页标题日期问题留待对应轮。

---
