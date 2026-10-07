# MEMORY HISTORY — 2026-10-07

> 由 `MEMORY.md` 清理周期移入（只保留最新两节），原样保留。

## 本次会话进展（r193：psKickTA 构造）

- 零硬件触碰。psKickTA 无铸造函数；锚点是真实 render 上下文
 （`+0xc`）；`+0x1c8` 由 PrepareTA 回填；features `+0x54` 门内
 第二次出现。下步手塑回合可在 fabricated shim 下离线做，
 无需硬件批准。
- 证据：`reports/r193-taskickta-shape.md`。候选下一步：手塑回合（离线）/ 活体 ladder（待批）。

---

---

## 本次会话进展（r194：psKickTA 手塑首轮）

- 零硬件触碰（fabricated）。harness 手塑 psKickTA 调通
 `RGXKickTA → 3` 干净退出：4 崩溃逐一定位（`+0x2d8/+0xb8/+0x28`）；
 关键纠偏——PrepareTA 偏移是元素制（`+0xb6` 实为字节 `0x2d8`）。
 下步造 `flag&2` 条目看 3 是否翻提交。GDB 翻车 3 则已记。
- 证据：`reports/r194-takick-shaping.md` + `r194-takick-return3.jsonl`。
 候选下一步：手塑回合 2（离线）/ 活体 observer（待批）。

---
