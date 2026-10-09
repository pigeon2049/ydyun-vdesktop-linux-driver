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






## r402 (2026-10-09): 测试 helper 提取（离线）

**重构**：新建 `tests/helpers.py`（`get_repo_root()` 由自身位置推导，与调用方目录深度无关；另有 `get_kernel_dir`/`get_scripts_dir`/`get_reports_dir`/`get_tests_dir`/`get_build_dir`/`get_kernel_header`）；75 文件的 repo-root 路径表达式（`ROOT`/`SOURCE`/`GUEST`/`TOOL`/`HEADER`/`KERNEL`/`SCRIPTS` 等，`.parents[2]` 与 `.parent.parent.parent` 两种写法）统一收敛；import 保持 discover/直接执行双模式（sys.path bootstrap）；修复 3 文件 `import sys` 后置导致的 NameError（bootstrap 恒自带）。

**门禁**：474+299 全绿；反向验证（helper 返回错一层→测试 FAIL）通过；直接执行双模式验证通过。

报告 mt-vgpu-guest/reports/r402-test-helpers-extracted.md。

## r401 (2026-10-09): 代码重构机会分析（离线）

**分析**：7 个机会按优先级——P0 测试 helper 提取（47 文件重复 ROOT preamble，2 种不一致写法，无共享模块）；P1 TA/3D submit 统一（mt_marker_submit_ta_work vs _3d_work 约 70% 相同，可提公共 ops+DM 参数化）；P2 长函数（pvr_translator_prepare_locked 294 行、pvr_bridge_dispatch 168 行）；P3 魔法超时值（5000/250/60000ms）命名；死代码零（静态函数全有引用，均为函数指针）；ENOTTY（dispatch 无此操作）vs EOPNOTSUPP（功能不支持）为有意区分；kernel 80 头文件重组暂缓（r400 已定）。

**门禁**：474+299 全绿（无代码改动）。

报告 mt-vgpu-guest/reports/r401-code-refactor-opportunities.md。

