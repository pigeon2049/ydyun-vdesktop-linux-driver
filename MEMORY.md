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
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

## 本轮进展（r333：出参判决，批准执行）

- GDB 活体单发 copy tq-perf（`=2` 窗口）：CF 一次命中（0/1/0/0，rsi=ctx 反证）→ abort 点槽值仍为 1——mismatch 解读死亡，abort 在下游。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 副产 `scripts/cf-slot-window.sh` 落库；GDB `printf` 不吃 python 变量教训。`bt 8` 无输出缺口记报告。
- 遗留：LookUpEOT 后状态（release 返回值或 job 差分），另行批准。
---

## 本轮进展（r331：头文件拼写收尾，离线）

- r332 草稿收尾：修双 `#else`（内核构建全灭）+ 9 处裸 `pr_info` 转 `mt_gpu_vm_log`（`mt_gpu_vm.h`×6、`mt_process_resources.h`×1、`mt_boot_bo.h`×2，复用宏）；新门禁 8 项（含反向）。
- 门禁：`check-offline` 394 Python + 299 C 全绿；`make kernel` W=1 零警告；`make check` 全绿（HEAD 上原是红色，止于 bootstrap `kvzalloc`）。`verify-mmu-bootstrap.py` 全过且 validation json 零 diff。
- 教训：诊断日志误放 `/tmp/opencode/`（84K，已清，`/tmp` 仅 1%）；后续易失产物走 `build/traces/<rNN>/`。`runtime-integration-build.json` 随 L2 刷新提交（旧凭证 272→299）。
- 遗留：r328 的 `[r8]` 出参读数（需重建活会话，待批准）；`mt_boot_bo.h:118 kzalloc` 未动。
---

