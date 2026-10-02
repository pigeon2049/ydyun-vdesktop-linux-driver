# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r86 Rogue2D 首选；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r86：真绘制栈 recon；离线）

- 用户令快速推进：GLES 无 EGL（0 导出），排除；Rogue2D 入选——
  96 导出、仅依赖 libsrv_um（同桥）、建 ctx→建面→填充→等 fence 四步、
  自带测试脚手架 + sutu 初始化。spike 序列已给（fabricated 先行）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r86-rogue2d-first-draw.md`。
- 遗留：Rogue2D fabricated spike；特性开关 + push 待批。

## 本次会话进展（r85：活体 KickTA，手工整形到墙；用户选 B）

- 批准的单次活体：passthrough KickTA 6/6 入 PrepareTA 深部；
  新崩溃 = p5 透传残留（真绘制状态指针，手工编不出）。
  桥引用 1 是 Chrome passive open（不杀，隔离无影响）。
- 结论：手工整形终结；提案真 GLES 绘制新项目（musa mesa 栈树内齐备，
  EGL 接线未知，需立项另批）。会后零残留。
- 证据：`mt-vgpu-guest/reports/r85-live-ta-shaping-wall.md` + jsonl。
- 遗留：真绘制改走 Rogue2D spike（r86 已 redirect）；特性开关 + push 待批。
