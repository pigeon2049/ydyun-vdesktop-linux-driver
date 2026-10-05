# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r167 L4 部分通过+rung5 阻断；freeze 继续）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r167：L4 部分通过，rung5 受阻）

- 真机调试已批准（手跑阶梯，桥零重载）。rung1–3 全绿（connect/device/devmemctx）；rung4 先 2 崩后 4 过；rung5 standalone 11/11 SIGSEGV、GDB 2/2 过。core 验尸：`RGXCreateRenderContextCCB+1525` 取 `r12+8==NULL`；148 条 trace 全 ret=0、内核零错误、probe ref 稳 1——桥无罪，UMD 侧时序敏感空指针，发布者未命名。
- 会话健康，freeze 继续；rung6–8 被阻；`translate_kick` 仍 off。证据：`reports/r167-l4-partial-segv.md` + trace + 验尸笔录。候选下一步：trace 加 tid 定位 NULL 发布者。

---

## 本次会话进展（r166：活体会話重建，批准执行）

- 真机调试已批准。`cold_disconnect` 0/1（idle/`guest=0 firmware=1`，双 clean rmmod）→ `fresh-trial --run --runtime-context` rc=0（trial `20261005T161706Z-cf0d876e`，fw sha `35d40f75…`，`guest=2 firmware=2` pinned ref=1）→ 桥默认加载（`card1`/`renderD128`）→ L3 全绿（node 0 failing/0 mismatch；dma smoke PASS，refs 平衡）。dmesg 无新增 WARN/BUG/Oops，r150 Oops 未复现但根因未命名。
- **Freeze**：probe ref 1、bridge ref 0，不 rmmod、不 unbind、不提交额外工作；`make probe`/`make umd` 继续禁用。证据：`reports/r166-session-rebuild.md`。候选下一步：L4 阶梯或 update 活体验证（需批准）。

---

