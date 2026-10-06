# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r182 bring-up 受阻；ref 漂移待查）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r182：TQX bring-up 受阻）

- 真机已批准（含内核改动+重载，均 ref0 操作，probe 会话零触碰语义）。flavor-1 上下文 + 3 Bo（prepare 内 seal 前）首活体报 `-22`（大声失败，无 GPU 动作）；单轮 probe ref 1→24（无残留/fd，功能完好，仅禁 unload；未解释）。代码 param 门控已入库（282+292，反向可抓）。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r182-tqx-blocked.md`。候选下一步：`-22` 定位与 ref 审计。

---

## 本次会话进展（r181：transfer dry-run 活体验证）

- 真机已批准（含内核改动+三次 `=2` 重载，均 ref0，probe 零触碰）。`translate_transfer` dry-run：首轮选择 bug（CCB PMR 胜出→错配大声失败）修复后，次轮程序 digest 与离线预言逐位一致（`0xd893...`）；附带修 fill 未初始化（双门禁）。278+292 全绿，反向全过。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r181-dryrun-verified.md`。候选下一步：真发射（scratch+fill+fence+回读）。

---

