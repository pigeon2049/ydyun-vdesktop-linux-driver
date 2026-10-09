# MEMORY-HISTORY-2026-10-09（只读归档；原样移入，不回改）

## r400 (2026-10-09): 代码目录重构完成（离线）

**重构**：Phase A 清理 kernel/ 三目录构建产物（gitignored）；Phase B tests 重组为 c/pvr/ta/guest/render/misc 子包（130 文件 git mv，修复 parents/include/Makefile 路径）；Phase C（80 头文件）评估暂缓；Phase D 更新 README 目录表。

**门禁**：474+299 全绿；make kernel W=1 零警告（28 模块）。

报告 mt-vgpu-guest/reports/r400-code-restructure.md。
---

## r399 (2026-10-09): R5 Phase 2 活体验证--无 ctx kick 返回 -EINVAL（活体）

**验证**：pre-live 门禁 T1/T2/T3 全绿后，bridge 重载到 r398 构建
（safe_rmmod.sh，probe 不动）。V1：无 ctx 的 0x82:0xC kick 返回 -EINVAL
（errno 22，dmesg 有 r398 Phase 2 标记）；V2：0x82:0x12 create
（handle=0x1000）后带 ctx kick 成功，OUT.update_fence=10 对应 dmesg
wire=10 精确匹配；V3：destroy 后 probe ref 13->25->13，delta 归零无泄漏。

**门禁**：474+299 全绿；make kernel W=1 零警告；dmesg 零 WARN/BUG/Oops。
本轮无代码改动（纯活体验证）。

报告 reports/r399-perfile-removed-live-verified.md。
---



## r402 (2026-10-09): 测试 helper 提取（离线）

**重构**：新建 `tests/helpers.py`（`get_repo_root()` 由自身位置推导，与调用方目录深度无关；另有 `get_kernel_dir`/`get_scripts_dir`/`get_reports_dir`/`get_tests_dir`/`get_build_dir`/`get_kernel_header`）；75 文件的 repo-root 路径表达式（`ROOT`/`SOURCE`/`GUEST`/`TOOL`/`HEADER`/`KERNEL`/`SCRIPTS` 等，`.parents[2]` 与 `.parent.parent.parent` 两种写法）统一收敛；import 保持 discover/直接执行双模式（sys.path bootstrap）；修复 3 文件 `import sys` 后置导致的 NameError（bootstrap 恒自带）。

**门禁**：474+299 全绿；反向验证（helper 返回错一层→测试 FAIL）通过；直接执行双模式验证通过。

报告 mt-vgpu-guest/reports/r402-test-helpers-extracted.md。

## r401 (2026-10-09): 代码重构机会分析（离线）

**分析**：7 个机会按优先级——P0 测试 helper 提取（47 文件重复 ROOT preamble，2 种不一致写法，无共享模块）；P1 TA/3D submit 统一（mt_marker_submit_ta_work vs _3d_work 约 70% 相同，可提公共 ops+DM 参数化）；P2 长函数（pvr_translator_prepare_locked 294 行、pvr_bridge_dispatch 168 行）；P3 魔法超时值（5000/250/60000ms）命名；死代码零（静态函数全有引用，均为函数指针）；ENOTTY（dispatch 无此操作）vs EOPNOTSUPP（功能不支持）为有意区分；kernel 80 头文件重组暂缓（r400 已定）。

**门禁**：474+299 全绿（无代码改动）。

报告 mt-vgpu-guest/reports/r401-code-refactor-opportunities.md。

