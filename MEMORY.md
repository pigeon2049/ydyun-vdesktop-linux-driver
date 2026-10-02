# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r85 活体 KickTA 到墙；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r85：活体 KickTA，手工整形到墙；用户选 B）

- 批准的单次活体：passthrough KickTA 6/6 入 PrepareTA 深部；
  新崩溃 = p5 透传残留（真绘制状态指针，手工编不出）。
  桥引用 1 是 Chrome passive open（不杀，隔离无影响）。
- 结论：手工整形终结；提案真 GLES 绘制新项目（musa mesa 栈树内齐备，
  EGL 接线未知，需立项另批）。会后零残留。
- 证据：`mt-vgpu-guest/reports/r85-live-ta-shaping-wall.md` + jsonl。
- 遗留：GLES 绘制立项；特性开关 + push 待批。

## 本次会话进展（r84：SubmitTA 回填映射；离线）

- T3 第三锹：两处桥调用 48 参数逐项回填；同步组装心脏
  （Query + tag 2/3 + builder + fence/update 双生成器）点名；
  重试语义（0x19→wait，我方 -ENOTTY 直接 break）澄清。
- fence/update 对偶与 check-only 首帧假设一致，无矛盾。
- 下步是执行验证二选一：A fabricated 同步整形 / B 活体 KickTA（待批）。
  零硬件触碰。证据：`mt-vgpu-guest/reports/r84-submitta-backfill.md`。
- 遗留：T3 执行验证；特性开关 + push 待批。
