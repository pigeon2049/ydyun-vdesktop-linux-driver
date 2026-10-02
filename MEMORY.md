# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r77 DDK2 rsi 身份落定；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r77：DDK2 rsi 身份落定；离线语料）

- §9 流程首验：DDK2 rsi = 走完完整创建的 kicksync 对象；
  `+0x8/+0x18/+0x28` = 注册 server ctx / `_SyncPrimAlloc` /
  `SubmissionBufAlloctorCreate`（语料行号 L28320/28321/28331）；
  render-obj 候选彻底排除；`param_4` 是可选 OUT，传 0 正确。
- 推论：DDK2 唯一可达路径 = 活体非零 CCB create 走完三步门控
  （待批实验已精确到"对象已知、只差一次活体 create"）。
- 零硬件触碰，无新 trace。证据：`mt-vgpu-guest/reports/r77-ddk2-rsi-identity.md`。
- 遗留：live 非零 CCB create + DDK2（待批）；真实 CCB 内容仍需绘制路径。

## 本次会话进展（AGENTS §9：Win 侧 + 反编译语料优先；用户指令）

- 用户要求：agent 提示优先参考 Windows 侧驱动实现与反编译工程。
  落为 §9 三条（语料→Win 包→Ghidra 工程复用不重跑；SHA 先对后用；
  语料是假设、执行是证据，与 §6.3 衔接）+ 检查单 +1 项；
  顺手把落点表 rNN 起点 r72→r77 订正。
- 纯文档改动，引用路径全存在。无 rNN 报告（非研究轮）。
