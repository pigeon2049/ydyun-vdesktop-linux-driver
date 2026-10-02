# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r79 全路径评估；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r79：vGPU 全路径梳理与方向评估；用户指令）

- 用户要完整梳理 + 方向 verdict：三线并行勘察 + 快照 §§3/8/9/10 对照。
- 结论：方向正确（翻译器是最小完备路径，accept-and-inspect 解耦关键），
  但结构性偏科——15 轮全在输入侧，T3（DM 格式）零进展，是最大风险；
  活体跑道基本见底（对象满/sealed/freeze），硬仗需新会话窗口。
- 建议顺序：T3 recon（离线语料）→ check-only 首帧设计（绕开 DDK2 的首胜路径）
  → 特性开关单独立项 → 会话更新窗口规划。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r79-path-review.md`。
- 遗留：按建议顺序推进（T3 recon 优先）；特性开关 + push 待批。

## 本次会话进展（r78：活体非零 CCB create 仍走 legacy；批准的单次实验）

- 用户批准真机：passthrough 非零 CCB create（pack `0x0733` 活体生效），
  其后 0 SyncPrim 调用 → legacy 分支；DDK2 同址崩（dmesg `at 48` 写 fault 吻合）。
  根因：桥 `mt_pvr_device.h:143-145` 故意钉 `features+0x54<2`（bring-up 刻意选择）。
- 决策：update/DDK2 从"缺输入"转为"需桥特性开关"（改代码+重编+重载，
  单独立项单独批准）；check 侧即翻译器当前完整输入；T3 仍被 CCB 卡住。
- 会后零残留（23/34/38/0 全对，D 态 0，无新增 WARN）。
  证据：`mt-vgpu-guest/reports/r78-live-ccb-legacy-path.md` + jsonl。
- 遗留：特性开关立项（待批）；真实 CCB 内容仍需绘制路径。
