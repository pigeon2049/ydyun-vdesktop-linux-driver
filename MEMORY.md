# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（AGENTS 新增 §9 参考实现优先序；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（AGENTS §9：Win 侧 + 反编译语料优先；用户指令）

- 用户要求：agent 提示优先参考 Windows 侧驱动实现与反编译工程。
  落为 §9 三条（语料→Win 包→Ghidra 工程复用不重跑；SHA 先对后用；
  语料是假设、执行是证据，与 §6.3 衔接）+ 检查单 +1 项；
  顺手把落点表 rNN 起点 r72→r77 订正。
- 纯文档改动，引用路径全存在。无 rNN 报告（非研究轮）。

## 本次会话进展（r76：ghidra 语料指路 DDK2；离线）

- 用户指路 `/opt/MTT-driver-only/` + `ghidra-projects/`：语料
  `decompiled/linux-legacy-umd-5.2.0/`（SHA 与在用 UMD 一致）成首选 RE 路径，
  伪 C 纠正 r74/r75 两处误读；以后 RE 结论先过语料。
- DDK2 `+0x28` = `SubmissionBufAlloctorCreate` 在 create 期填入
  （门控：features>=2 + SyncPrim 两步；`+0x48` 出生置零即崩溃点形状）。
  r75 的 render-obj 候选被取代（旧报告留档不改）。
- CCB pack 公式 trace 实测：`ui32PackedCCBSizeU88=(arg5&0xff)<<8|(arg4&0xff)`。
- 边界：fabricated 恒走 legacy 分支，DDK2 离线不可驱动；
  下一步 = 活体 passthrough 非零 CCB create（需单独批准）。
- 证据：`mt-vgpu-guest/reports/r76-ghidra-ddk2-submissionbuf.md` +
  `r76-ccb-size-pack.jsonl`。
- 遗留：live 非零 CCB create + DDK2（待批）；真实 CCB 内容仍需绘制路径。
