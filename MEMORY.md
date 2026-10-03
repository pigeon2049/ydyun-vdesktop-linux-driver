# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-04（r126 首帧 fabricated 门禁；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r126：r113 首帧 fabricated 门禁；离线）

- 新增 `tests/test_r113_first_frame_envelope.py` 8 项全绿并反向验证；
  L1 226→234，STATUS/快照 §6 计数已同步；会话仍 freeze。
- 新发现：模板 `0x4668=0xed00000000` 直通（活体风险，已标注）；
  STATUS 桥 build-id 旧值攒入刷新 pass。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r126-first-frame-gate.md`。
- 遗留：活体首帧执行待明确批准；62 提交未 push；STATUS 桥 id 刷新。

---

## 本次会话进展（r125：新会话重建完成；批准执行）

- cold-disconnect（finish=0/1）→ fresh-trial（2/2 retained）
  → 桥加载（build-id `2c6bede3…`）→ L3 全绿 → L4 八级全 0。
- 终态：probe 引用 1 / bridge 引用 0，dmesg 干净，无 D 任务；
  会话即刻起 freeze。证据：`mt-vgpu-guest/reports/r125-session-rebuild.md`。
- 遗留：r113 首帧执行下一轮单独确认；61 提交未 push（用户明确暂不 push）。
