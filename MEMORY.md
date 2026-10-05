# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r169 桥字节级无罪；GDB 遮罩未解释）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r169：OUT 字节级对比）

- 真机已批准（手跑，桥零重载）。10 组桥命令 OUT 全量对比：18 处差异全为调用方指针回显，其余逐字节一致——桥彻底无罪。另否 argv[0] 与重试（rung5 standalone 累计 0/20，GDB 5/5）。遮罩机制未解释，会话健康 freeze 继续。
- 证据：`reports/r169-byte-exoneration.md` + 失败 trace。候选下一步：GDB 监督下跑梯（非常规但诚实）解 rung6–8。

---

## 本次会话进展（r168：trace 加 tid 猎杀 NULL 发布者）

- 真机已批准（手跑，桥零重载）。shim 25 处记录加 `tid`（门禁断言+反向注零验证；diff 归一化纯净）。活体对比：失败/通过轮均为单 tid——交错假设证伪；桥前缀 65/65 一致全 ret=0。ASLR 关/单 CPU/perturb 0/165 对照仍全崩；GDB 4/4 过。发布者未命名，会话健康 freeze 继续。
- 证据：`reports/r168-tid-hunt.md`。候选下一步：GDB 条件断点比对句柄值。

---

