# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](memory/MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](memory/MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](memory/MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](memory/MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](memory/MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](memory/MEMORY-HISTORY-2026-10-07.md)。
> 2026-10-08 起归档于 [`MEMORY-HISTORY-2026-10-08.md`](memory/MEMORY-HISTORY-2026-10-08.md)。
> r363 轮按 §4 清理：r361 节已移入归档。
> r364 轮按 §4 清理：r362 节已移入归档。
> r365 轮按 §4 清理：r363 节已移入归档。
> r368 轮按 §4 清理：r364 节已移入归档。
> r369 轮按 §4 清理：r365 节已移入归档。
> r370 轮按 §4 清理：r366 节已移入归档。
> r372 轮按 §4 清理：r367 节已移入归档。
> r373 轮按 §4 清理：r368 节已移入归档。
> r378 轮按 §4 清理：r376 节已移入归档。
> r374 轮按 §4 清理：r369 节已移入归档。
> r379 轮按 §4 清理：r377 节已移入归档。
> r380 轮按 §4 清理：r378 节已移入归档。
> r382 轮按 §4 清理：r379 节已移入归档。
> r383 轮按 §4 清理：r380 节已移入归档。
> r384 轮按 §4 清理：r381 节已移入归档。
> r385 轮按 §4 清理：r382 节已移入归档。
> r386 轮按 §4 清理：r385 节已移入归档。
> r387 轮按 §4 清理：r383 节已移入归档。
> r388 轮按 §4 清理：r386、r384 节已移入归档。
> r390 轮按 §4 清理：r387 节已移入归档。
> r391 轮按 §4 清理：r388 节已移入归档。
> r392 轮按 §4 清理：r389 节已移入归档。
> r393 轮按 §4 清理：r390 节已移入归档。
> r394 轮按 §4 清理：r391、r392 节已移入归档。
> r395 轮按 §4 清理：r393 节已移入归档。
> r396 轮按 §4 清理：r394 节已移入归档。
> r398 轮按 §4 清理：r395 节已移入归档。
> r402 轮按 §4 清理：r400、r399 节已移入归档。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。
> r404 轮按 \u00a74 清理：r402、r401 节已移入归档。
> r405 轮按 §4 清理：无（仅 r404、r405 两节，保留）。



## r406 (2026-10-09): 3D 包活体提交——固件无响应（忽略签名）

**活体单发**：pre-live T1/T2/T3 全过， 白名单确认；桥侧测试钩子直接调用 （绕过 ，门控保持 0）；3D 包（opcode 0x68 @+0x0c，VA @+0x28，size @+0x30，wire_id @+0x48）提交成功，fence 已分配；**固件 5s 内无完成事件**（ 返回 0，），签名 submitted-but-ignored。

**结论**：3D 基础设施（包格式/提交路径/fence）工作正常；固件需要真实 3D 负载，非 marker 包。与 r380（DM2 忽略空 marker）、r405 G1 一致。

**状态**：测试桥仍在载（ref=1，pending 3D fence 持有，无法卸载）；源码已恢复 r404（钩子未提交）；系统稳定，无 oops；待用户冷重启清除。

报告 mt-vgpu-guest/reports/r406-3d-live-ignored.md。

## r405 (2026-10-09): 真机适配缺口分析（离线）

**"真实可用"四级定义**：T0 API 不报错（r144/r172 已达，123 调用零非零）→ **T1 GPU 真实执行（核心缺口：仅 marker 空包，零真实 TA/3D 命令）** → T2 像素可验证（无 DDK2 回读基础设施）→ T3 UMD 集成（r358 建连/r172 全链通，但 TA/3D 从未真实执行）；T4 桌面栈明确 out-of-scope。

**缺口清单（优先级）**：G1 真实 payload（P0，3–5 轮：TA 命令包布局未知，r174 只捕获了 TDM CCB）> G3 门控（P1：`MT_3D_SUBMIT_GATE`=0 钉死，3D 零活体；`0x82:0x14` 仍 observer）> G4 回读验证（P2：PMR→mmap 路径可行，需 userspace PPM 工具）> G2 UMD 集成（P1：依赖 G1）> G6 长尾（kick_pr=1、sync-prim 真实路径、16MB stride 待验证）> G5 压力（P3）。

**路线图**：r406 3D marker 活体 → r407 TA payload 捕获 → r408 包解析 → r409 真实 TA 活体（高风险，需冷重启预案）→ r410 回读工具 → r411 3D 门控 → r412 UMD triangle。

报告 mt-vgpu-guest/reports/r405-real-usability-gap-analysis.md。

