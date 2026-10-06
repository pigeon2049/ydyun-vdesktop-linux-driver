# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r180 scratch 接线规约；实现待 `=2` 窗口）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r180：T3-transfer 活体接线规约）

- 只读 recon，零硬件触碰，无代码改动，会话未碰（probe 1）。决定性依据：外页 BO 绑不进会话空间（store/ops 一致性）；scratch 中转只用已验证原语（分配/绑定/fill 构造/submit+fence/CPU 落位）。VA 建议 `0x49000000`；copy 双面；门禁计划已列。
- 证据：`reports/r180-transfer-wiring.md`。候选下一步：实现轮（`=2` 窗口 + 像素回读）。

---

## 本次会话进展（r179：T3-transfer fill-input 构造器）

- 零硬件触碰（离线实现+门禁，会话未碰）。新增 `mt_transfer_fill.h`：池解析（空/短/错位全拒）+ 矩形构造（`w*h` 错配大声拒绝）；C 门禁 16 项（274+288 全绿），反向掐校验可抓。颜色原搬不解释；W/H 由调用方给。未接桥无发射。
- 证据：`reports/r179-fill-builder.md`。候选下一步：活体接线（`=2` 窗口 + 像素回读）。

---

