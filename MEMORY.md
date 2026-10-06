# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r181 dry-run 活体验证；真发射下一轮）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r181：transfer dry-run 活体验证）

- 真机已批准（含内核改动+三次 `=2` 重载，均 ref0，probe 零触碰）。`translate_transfer` dry-run：首轮选择 bug（CCB PMR 胜出→错配大声失败）修复后，次轮程序 digest 与离线预言逐位一致（`0xd893...`）；附带修 fill 未初始化（双门禁）。278+292 全绿，反向全过。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r181-dryrun-verified.md`。候选下一步：真发射（scratch+fill+fence+回读）。

---

## 本次会话进展（r180：T3-transfer 活体接线规约）

- 只读 recon，零硬件触碰，无代码改动，会话未碰（probe 1）。决定性依据：外页 BO 绑不进会话空间（store/ops 一致性）；scratch 中转只用已验证原语（分配/绑定/fill 构造/submit+fence/CPU 落位）。VA 建议 `0x49000000`；copy 双面；门禁计划已列。
- 证据：`reports/r180-transfer-wiring.md`。候选下一步：实现轮（`=2` 窗口 + 像素回读）。

---

