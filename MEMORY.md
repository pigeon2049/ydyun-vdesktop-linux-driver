# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-04（r127 活体首帧成功；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r127：活体首帧执行成功；批准执行）

- 单帧 DM2 空包（frame_tag=1，无 RT）：seq=1，completed 0→1，
  faulted=0；`0x4668` 直通值被接受；模块已卸（留 2 条 r44 同类 WARN）。
- probe 引用 stays 35（已知泄漏类）；桥探针复核全绿，会话健康，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r127-first-frame-live.md`。
- 遗留：T3 下一步（非零 CCB 仍缺，STATUS 第 1 项）；63 提交未 push。

---

## 本次会话进展（r126：r113 首帧 fabricated 门禁；离线）

- 新增 `tests/test_r113_first_frame_envelope.py` 8 项全绿并反向验证；
  L1 226→234，STATUS/快照 §6 计数已同步；会话仍 freeze。
- 新发现：模板 `0x4668=0xed00000000` 直通（活体风险，已标注）；
  STATUS 桥 build-id 旧值攒入刷新 pass。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r126-first-frame-gate.md`。
- 遗留：活体首帧执行待明确批准；62 提交未 push；STATUS 桥 id 刷新。
