# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r185 快照刷新；未 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r185：快照刷新 pass）

- 零硬件触碰。r164 后积压 20 轮，快照 §1/§2/§5/§6/§11 已同步 r165–r184
 （门禁实测 282 + 292；pmr 门禁 11→58；§5 重写为 ref 收尾 → 真发射 → update → TA/CDM）。§12 历史计数为同期记录，不动。
- 证据：`reports/r185-snapshot-refresh.md`。候选下一步：等硬件批准（kill-while-busy 关账 / 真发射 / `=2` update 验证）。

---

## 本次会话进展（r184：defaults 活体差分 Δ0）

- 批准执行单次活体（无重载、无 GPU 工作：legacy `0x89:0x0 → -25` 后 SIGABRT）。真实 blit（passthrough 记录，UMD SHA `b3058c02…`）：9 maps/11 mmaps/abort/close 后 probe 66→66、bridge 1→1（Δ0），无 D 态。maps 无罪；+65 与 prepare/挂起强相关——r183 假设修正为条件触发（kill-while-busy 命中 destroy `-EBUSY` 才 abandon；干净 abort 不触发）。
- 证据：`reports/r184-defaults-differential-d0.md` + `r184-defaults-blit.jsonl`。候选下一步：kill-while-busy 关账轮（待批）→ 真发射。

---

