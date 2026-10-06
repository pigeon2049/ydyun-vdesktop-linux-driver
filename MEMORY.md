# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r178 几何通道落定；T3-transfer 可开原型）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r178：几何/颜色通道落定）

- 零硬件触碰，无代码改动（离线 fabricated + GDB 转储）。排除 CCB/context/注解；5MB 池实转储：3841 零头 + `ff0000ff`×1310720（=1280×1024，行连续）+ 254 零尾；64×64 复核池代数精确成立。颜色即像素字；源池空与 fill 一致。
- 证据：`reports/r178-geometry-channel.md`（二进制未入库，数字即证据）。门禁复核 274+272。候选下一步：T3-transfer 原型（dst VA + 全表面 + 像素字 → TQX fill）。

---

## 本次会话进展（r177：T3 输出侧盘点）

- 纯只读盘点，零硬件触碰，无代码改动，会话未碰（probe 1/bridge 0）。输出侧：DM2 空 marker、TQX fill 矩形、TQX copy 计划均有发射能力；Transfer 归 TQX（DM 只欠 TA/3D）。CCB 几乎全指针/标志，颜色几何不在其中——首要缺口；payload B 的 `0xa3xxxx` 归属未定。
- 证据：`reports/r177-output-inventory.md`。门禁复核 274+272 全绿。候选下一步：追踪 transfer surface 对象。

---

