# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r116 警告清零；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r116：4 旧警告清零；离线自主）

- drain_pending 删无用 bar1 块、ops 加 __maybe_unused、
  fix_poll 补 DESCRIPTION；kernel exit 0 零警告；
  L1 226+268 全绿；反向验证通过。代码提交 `d3b7453`。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r116-warnings-clean.md`。
- 遗留：加载窗口；T3 首帧执行；push 待批。

## 本次会话进展（r115：快照刷新 pass；离线自主）

- §4 定期刷新：§§1/2/5/6/11/12 + STATUS 下一步合入 r72–r114；
  §3/4/7/8/9/10 封存未动；在盘桥含 TDM（与在载不同）已记入 §12。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r115-snapshot-refresh.md`。
- 遗留：加载窗口；T3 首帧执行；push 待批。
