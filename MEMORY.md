# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r80 全景测试评估；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r80：全景测试评估；用户指令）

- 用户要"分配→复制→渲染→执行"逐段测评：L1 本轮实跑全绿（221+268）；
  `make kernel` exit 0；dmesg 0 WARN；活体 sysfs 与 §12 一致；零硬件触碰。
- 新发现：干净重编有 4 处 r38 期旧警告（快照"零警告"须加"增量"限定，
  或另起小轮清掉）。
- 结论：分配/复制/合成渲染/手工执行全绿；缺口 = T3（主）、CCB 内容、
  特性开关、ZSBuffer、check-only 首帧假设、会话窗口。
- 证据：`mt-vgpu-guest/reports/r80-panorama.md`。
- 遗留：T3 recon 优先；旧警告清理小轮；特性开关 + push 待批。

## 本次会话进展（r79：vGPU 全路径梳理与方向评估；用户指令）

- 用户要完整梳理 + 方向 verdict：三线并行勘察 + 快照 §§3/8/9/10 对照。
- 结论：方向正确（翻译器是最小完备路径，accept-and-inspect 解耦关键），
  但结构性偏科——15 轮全在输入侧，T3（DM 格式）零进展，是最大风险；
  活体跑道基本见底（对象满/sealed/freeze），硬仗需新会话窗口。
- 建议顺序：T3 recon（离线语料）→ check-only 首帧设计（绕开 DDK2 的首胜路径）
  → 特性开关单独立项 → 会话更新窗口规划。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r79-path-review.md`。
- 遗留：按建议顺序推进（T3 recon 优先）；特性开关 + push 待批。
