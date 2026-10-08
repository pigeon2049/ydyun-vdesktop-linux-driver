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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。






## r395 (2026-10-08): 安全网首个实战检验——pre-live 门禁全绿 + TA 回归双 kick 通过（活体）

**背景**：r394 落地的 T1/T2/T3 安全测试首次作为 pre-live 门禁执行。用户要求真机调试前必须跑安全门禁。

**门禁**：check-offline 472+299 全绿（含 9 个新安全测试）；scripts/safe_rmmod.sh 存在且可执行（0755，refcount 守卫）；T1（VM 完整性）/T2（opcode 白名单，(3,0x66) 钉死 DM3）/T3（卸载安全）全部通过；pre-live 清单 5 条逐项核对。

**活体**：两次真实 0x82:0xC TA-only kick（r377 既证 harness：INIT module=2 + bridge 0xc0206440，IN kick_ta=1@188）。ioctl 均返回 0，OUT.error=0，OUT.update_fence=4/5 与 dmesg submitted wire=4/5 精确匹配；零 "completion timeout"（0x100 完成事件到达，走 mt_marker_complete_ta 正常退休）；dmesg 零 WARN/BUG/Oops；refs 不变（bridge 0 / probe 13 基线）。

**安全合规**：本轮零模块操作（未重载、未卸载、未重启）；rmmod -f 零出现；timeout 未使用；两次 kick 串行。

**诚实边界**：marker 级 TA 路径（零绘制）；两次 kick 均走 per-file 回退（ctx=0x0），per-context 路径已在 r391 V3 验证；probe ref=13 基线含 r389 旧泄漏，本轮 delta 为 0。

报告 reports/r395-safety-net-first-live-test.md，证据 reports/r395-dmesg-ta-regression.txt。

## r394 (2026-10-08): live 前安全测试落地——三类事故各有门禁拦截（离线）

**背景**：用户要求"调试前增加更多测试，避免弱智错误导致冷重启"。今日三次冷重启：r375（手动拼装 mt_gpu_vm → oops）、r380（DM2 试探 0x66 → firmware 清 trial）、强制卸载 probe（ref=1 状态不一致 → 内核挂起）。

**实现**：T1 `tests/test_vm_init_integrity.py`（VM 内部字段赋值禁区扫描，2 tests）；T2 `tests/test_opcode_whitelist.py`（`(dm,opcode)` PROVEN 白名单 + `(2,0x66)` 永禁，TA 钉死 DM3、3D 钉死 DM2，4 tests）；T3 `tests/test_pre_live_safety.py`（强制卸载仓库黑名单 + `scripts/safe_rmmod.sh` refcount 守卫 + T1/T2 齐套断言，3 tests）+ pre-live 检查清单（5 条）。

**门禁**：472 Python + 299 C 全绿（+9 新测试）；反向验证 RV1（注入 `vm->ranges=`→T1 FAIL）、RV2（`MT_FW_DM_TA`→2U→T2 FAIL）、RV3（注入强制卸载命令→T3 FAIL）全部通过；本轮无内核代码改动故未跑 `make kernel`。

**诚实边界**：静态扫描覆盖 in-tree 代码；一次性探针模块（gitignored）靠清单人工执行；`(1,100)` 经 `mt_live_marker` 参数可打 dm=2/3，静态无法钉死，live 简报须声明实际 dm。

报告 `reports/r394-live-safety-tests.md`。

