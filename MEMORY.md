# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r170 GDB 监督下 L4 全绿；freeze 继续）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r170：GDB 监督下 L4 八级全绿）

- 真机已批准（手跑，桥零重载；监督非常规，如实记录）。rung5 syncprim、rung6 kicksync 建销、rung7 compute 建销、rung8 `RGXKickSync→0`（inspect，无 GPU 工作）逐级全绿；四轮 trace 全 ret=0；probe ref 稳 1；dmesg 干净。rung5 standalone 崩溃仍在。
- 会话健康 freeze 继续；`translate_kick` 仍 off。证据：`reports/r170-supervised-ladder.md` + rung8 trace。候选下一步：同监督跑 DDK2 全链复验。

---

## 本次会话进展（r169：OUT 字节级对比）

- 真机已批准（手跑，桥零重载）。10 组桥命令 OUT 全量对比：18 处差异全为调用方指针回显，其余逐字节一致——桥彻底无罪。另否 argv[0] 与重试（rung5 standalone 累计 0/20，GDB 5/5）。遮罩机制未解释，会话健康 freeze 继续。
- 证据：`reports/r169-byte-exoneration.md` + 失败 trace。候选下一步：GDB 监督下跑梯（非常规但诚实）解 rung6–8。

---

