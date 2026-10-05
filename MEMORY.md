# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r168 tid 猎杀；交错已排除）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r168：trace 加 tid 猎杀 NULL 发布者）

- 真机已批准（手跑，桥零重载）。shim 25 处记录加 `tid`（门禁断言+反向注零验证；diff 归一化纯净）。活体对比：失败/通过轮均为单 tid——交错假设证伪；桥前缀 65/65 一致全 ret=0。ASLR 关/单 CPU/perturb 0/165 对照仍全崩；GDB 4/4 过。发布者未命名，会话健康 freeze 继续。
- 证据：`reports/r168-tid-hunt.md`。候选下一步：GDB 条件断点比对句柄值。

---

## 本次会话进展（r167：L4 部分通过，rung5 受阻）

- 真机调试已批准（手跑阶梯，桥零重载）。rung1–3 全绿（connect/device/devmemctx）；rung4 先 2 崩后 4 过；rung5 standalone 11/11 SIGSEGV、GDB 2/2 过。core 验尸：`RGXCreateRenderContextCCB+1525` 取 `r12+8==NULL`；148 条 trace 全 ret=0、内核零错误、probe ref 稳 1——桥无罪，UMD 侧时序敏感空指针，发布者未命名。
- 会话健康，freeze 继续；rung6–8 被阻；`translate_kick` 仍 off。证据：`reports/r167-l4-partial-segv.md` + trace + 验尸笔录。候选下一步：trace 加 tid 定位 NULL 发布者。

---

