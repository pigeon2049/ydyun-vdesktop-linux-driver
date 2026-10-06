# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r171 update 注入证伪；DDK2 重载待批）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r171：update 活体注入证伪）

- 真机已批准（手跑+GDB 监督，桥零重载）。rung9（wire IN 摆 update_count=1）`RGXKickSync→1` 且无桥调用；fence 名/PMR 柄变体同败。语料：b26 是 UMD CMD 对象（count 在 +0xD8），84B 错位；该函数无 update 组装（r74 独立 corroborate）。活体验证需 DDK2 重载，被 freeze 挡，单独立项待批。
- 会话健康 freeze 继续；`translate_kick` 仍 off。证据：`reports/r171-update-inject-refuted.md` + trace。

---

## 本次会话进展（r170：GDB 监督下 L4 八级全绿）

- 真机已批准（手跑，桥零重载；监督非常规，如实记录）。rung5 syncprim、rung6 kicksync 建销、rung7 compute 建销、rung8 `RGXKickSync→0`（inspect，无 GPU 工作）逐级全绿；四轮 trace 全 ret=0；probe ref 稳 1；dmesg 干净。rung5 standalone 崩溃仍在。
- 会话健康 freeze 继续；`translate_kick` 仍 off。证据：`reports/r170-supervised-ladder.md` + rung8 trace。候选下一步：同监督跑 DDK2 全链复验。

---

