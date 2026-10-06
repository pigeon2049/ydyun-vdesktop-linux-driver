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

最后更新：2026-10-06（r191 kill-while-busy Δ0；未 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r191：kill-while-busy 关账轮）

- 批准执行活体（无重载：`rmmod` 被会话工具链持有挡回 EBUSY）。
 GDB 监督 #104 mmap 处击杀（死时 3 MAPs live），事后 66→66（Δ0），
 无 D 态，L3 复绿。file_release 假设至此无活体支持；+65 仍未命名，
 边界收紧（maps/abort/击杀/prepare 记账/残留进程全排除）。
 7 活体轮零新增泄漏。
- 证据：`reports/r191-killbusy-d0.md` + `r191-killbusy.jsonl`。候选下一步：解持有后 `=2` 轮 / 真发射。

---

## 本次会话进展（r190：update 路径定位）

- 零硬件触碰。语料按名定位：非零 update 走 TA 链
 `RGXKickGfx→SubmissionSetUpdateSyncPrim→BridgeRGXKickTA3D5`
 （0x82:0x14，IN 108/OUT 4）；桥无此 handler，requirements 表亦
 `CMD_LAST`——即 STATUS 第二项的 concrete 缺口。活体计划已列
 （observer + producer 待定）；producer 本身仍 open。
- 证据：`reports/r190-update-path-recon.md`。候选下一步：producer recon（离线）或等硬件批准。

---

