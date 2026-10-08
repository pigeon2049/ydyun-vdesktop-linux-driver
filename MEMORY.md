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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。






## r394 (2026-10-08): live 前安全测试落地——三类事故各有门禁拦截（离线）

**背景**：用户要求"调试前增加更多测试，避免弱智错误导致冷重启"。今日三次冷重启：r375（手动拼装 mt_gpu_vm → oops）、r380（DM2 试探 0x66 → firmware 清 trial）、强制卸载 probe（ref=1 状态不一致 → 内核挂起）。

**实现**：T1 `tests/test_vm_init_integrity.py`（VM 内部字段赋值禁区扫描，2 tests）；T2 `tests/test_opcode_whitelist.py`（`(dm,opcode)` PROVEN 白名单 + `(2,0x66)` 永禁，TA 钉死 DM3、3D 钉死 DM2，4 tests）；T3 `tests/test_pre_live_safety.py`（强制卸载仓库黑名单 + `scripts/safe_rmmod.sh` refcount 守卫 + T1/T2 齐套断言，3 tests）+ pre-live 检查清单（5 条）。

**门禁**：472 Python + 299 C 全绿（+9 新测试）；反向验证 RV1（注入 `vm->ranges=`→T1 FAIL）、RV2（`MT_FW_DM_TA`→2U→T2 FAIL）、RV3（注入强制卸载命令→T3 FAIL）全部通过；本轮无内核代码改动故未跑 `make kernel`。

**诚实边界**：静态扫描覆盖 in-tree 代码；一次性探针模块（gitignored）靠清单人工执行；`(1,100)` 经 `mt_live_marker` 参数可打 dm=2/3，静态无法钉死，live 简报须声明实际 dm。

报告 `reports/r394-live-safety-tests.md`。

## r393 (2026-10-08): R6-6 调研结论——server 侧 TDM context 不需要真实化（离线）

结论：0x89:0x8（RGXTDMCreateTransferContext2）空 token 已足够，R6-6 关闭为 wont-do by design。TDM=Transfer Data Manager（2D/blit 引擎，KMD 头 common_musaxfer_bridge.h）。r150 活体证：真实 UMD 的 TDM 全生命周期（0x89:0x8 create → 0x89:0xa submit → 0x89:0x9 destroy）全 ret=0，UMD 正常推进；r174 活体：0x89:0xa accept-and-log 从真实 UMD 捕获到非零 CCB 字节。submit 仅做存在性校验（have_ctx），不读 context 状态；唯一真实的 TDM 资源是 shared-memory PMR（0x89:0x5，CLI+USC 独立，已实现）。与 R6 独立，不阻塞真实 UMD。若未来实现真实 TDM 执行可重开。

门禁：check-offline 全绿（纯调研无代码改动）。

报告 reports/r393-tdm-no-server-state-needed.md，证据 reports/r393-evidence.txt。

