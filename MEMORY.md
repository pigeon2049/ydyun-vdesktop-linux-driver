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

最后更新：2026-10-06（r184 defaults 差分 Δ0；未 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r184：defaults 活体差分 Δ0）

- 批准执行单次活体（无重载、无 GPU 工作：legacy `0x89:0x0 → -25` 后 SIGABRT）。真实 blit（passthrough 记录，UMD SHA `b3058c02…`）：9 maps/11 mmaps/abort/close 后 probe 66→66、bridge 1→1（Δ0），无 D 态。maps 无罪；+65 与 prepare/挂起强相关——r183 假设修正为条件触发（kill-while-busy 命中 destroy `-EBUSY` 才 abandon；干净 abort 不触发）。
- 证据：`reports/r184-defaults-differential-d0.md` + `r184-defaults-blit.jsonl`。候选下一步：kill-while-busy 关账轮（待批）→ 真发射。

---

## 本次会话进展（r183：ref 漂移离线审计）

- 零硬件触碰（只读代码审计 + `lsmod` 只读；未重载模块、未提交 GPU 工作；`dmesg` 本容器无权读）。首要嫌疑已命名：`pvr_file_release` 双 early-return（unbind/destroy 失败即 `return`，`mt_pvr_bridge.c:868-875`）可 abandon 整文件 PMR 的 `dma_owner`（每 map +1），量级 ≈14/轮与失败轮 +14 同形；活体 probe Used by=66（=1+65）只读吻合。prepare 失败路/DMA 注册释放经走查配平，已排除。以上为推断，活体差分待可重载窗口（需批准）；释放语义未动。
- 证据：`reports/r183-ref-audit-offline.md`。候选下一步：活体差分（defaults legacy 差分，需批准）→ 真发射。

---

