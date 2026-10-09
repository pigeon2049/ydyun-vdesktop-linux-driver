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


## r404 (2026-10-09): Dispatch 拆分（离线）

**重构**：`pvr_bridge_dispatch()`（168 行）按 bridge group 拆为 8 个 helper（`pvr_dispatch_srvcore`/`sync`/`mm`/`rgxcompute`/`rgxta3d`/`rgxkicksync`/`rgxhwperf`/`rgxtdm`），主函数仅保留 `-ENOTCONN` 检查+外层路由；`pvr_translator_prepare_locked`（294 行）经分析不拆（线性 6 阶段已清晰，`pvr_translator_teardown_locked()` 集中回滚，拆分会退化 `fail_at` 调试信息；r401 原评估为"可选"）。

**测试**：13 个文本扫描测试适配 helper 位置（`fn_body(src, 'pvr_dispatch_*')` 替代 `case MT_PVR_BRIDGE_*:` 块提取）；`test_pvr_fn_ids.py` 提取范围扩大覆盖 8 helpers。

**门禁**：474+299 全绿；`make kernel` W=1 零警告；反向验证（helper 内改返回 `-ENOTTY`→测试 FAIL）通过。

报告 mt-vgpu-guest/reports/r404-dispatch-split.md。




## r405 (2026-10-09): 真机适配缺口分析（离线）

**"真实可用"四级定义**：T0 API 不报错（r144/r172 已达，123 调用零非零）→ **T1 GPU 真实执行（核心缺口：仅 marker 空包，零真实 TA/3D 命令）** → T2 像素可验证（无 DDK2 回读基础设施）→ T3 UMD 集成（r358 建连/r172 全链通，但 TA/3D 从未真实执行）；T4 桌面栈明确 out-of-scope。

**缺口清单（优先级）**：G1 真实 payload（P0，3–5 轮：TA 命令包布局未知，r174 只捕获了 TDM CCB）> G3 门控（P1：`MT_3D_SUBMIT_GATE`=0 钉死，3D 零活体；`0x82:0x14` 仍 observer）> G4 回读验证（P2：PMR→mmap 路径可行，需 userspace PPM 工具）> G2 UMD 集成（P1：依赖 G1）> G6 长尾（kick_pr=1、sync-prim 真实路径、16MB stride 待验证）> G5 压力（P3）。

**路线图**：r406 3D marker 活体 → r407 TA payload 捕获 → r408 包解析 → r409 真实 TA 活体（高风险，需冷重启预案）→ r410 回读工具 → r411 3D 门控 → r412 UMD triangle。

报告 mt-vgpu-guest/reports/r405-real-usability-gap-analysis.md。



## r406 (2026-10-09): 3D 包活体提交——固件无响应（忽略签名）

**活体单发**：pre-live T1/T2/T3 全过， 白名单确认；桥侧测试钩子直接调用 （绕过 ，门控保持 0）；3D 包（opcode 0x68 @+0x0c，VA @+0x28，size @+0x30，wire_id @+0x48）提交成功，fence 已分配；**固件 5s 内无完成事件**（ 返回 0，），签名 submitted-but-ignored。

**结论**：3D 基础设施（包格式/提交路径/fence）工作正常；固件需要真实 3D 负载，非 marker 包。与 r380（DM2 忽略空 marker）、r405 G1 一致。

**状态**：测试桥仍在载（ref=1，pending 3D fence 持有，无法卸载）；源码已恢复 r404（钩子未提交）；系统稳定，无 oops；待用户冷重启清除。

报告 mt-vgpu-guest/reports/r406-3d-live-ignored.md。

## r407 (2026-10-09): TA 命令缓冲捕获机制验证（活体观察）

**活体观察**：冷重启后系统干净（probe trial restored，bridge ref=0）；pre-live T1/T2/T3 全过；桥侧临时 hook（`copy_from_user` + `print_hex_dump`）在 `0x82:0xC` 入口成功捕获 TA 命令缓冲前 64 字节（`ta_size=360` 与 r363 一致）；捕获为 fabricated 测试模式，机制已验证；hook 已移除，源码恢复，桥重载干净构建；dmesg 无 WARN/BUG/Oops。

**TA 包布局（已知）**：DM 包 80B（`MT_FW_COMMAND_BYTES`，opcode 0x66 @+0x0c，wire_id @+0x48，pid @+0x4c，r365 活体验证）；TA 命令缓冲 360B（r363 实测，内容为 TA 指令流，布局待反汇编 `RGXSubmitTA` 解析）；0x82:0xC IN 268B（p_ta_cmd @120，kick_ta @188，ta_cmd_size @264）。

**诚实边界**：捕获的是 fabricated 模式，非真实 UMD 负载（无 3D 应用）；真实 TA 指令流布局未解析；本轮未提交 GPU 工作。

**下一步**：r408 TA 包解析（离线，反汇编分析）。



## r408 (2026-10-09): RGXSubmitTA 不构造 TA 缓冲（离线反汇编）

**反汇编**（MUSA DDK 5.2.0 UMD，零硬件）：调用链 RGXKickTA(0x17afd0)→RGXPrepareTA(FUN_00178800,218行)→RGXSubmitTA(FUN_001796b0,709行)→BridgeRGXKickTA3D3(FUN_00137180)→SubmitTADataEnQueue；SubmitTA内0x168=360为立即数尺寸参数、VA取自psKickTA[0]，全函数无缓冲写入/模板填充；PrepareTA只做指针搬运(psKickTA[0..2]=render_ctx+0xb6/b8/ba)+0x1c8-0x1d4状态回填；psKickTA无UMD侧构造函数(r193 corroborate)。

**结论**：360B TA缓冲内容(TA指令流)由客户端3D状态机在调用前生成，RGXSubmitTA只透传VA；r409的最小真实TA包不能从本反汇编直接得到。

**r409方案**(按推荐序)：真实应用trace(用r407 hook捕获真实360B，前置：有3D应用能走到RGXKickTA) > KMD/固件文档找TA命令格式 > 盲探(不推荐，r380教训)。

**诚实边界**：已确认=调用链/VA透传/PrepareTA回填；推断=psKickTA+0x08/+0x10语义、DM包@+0x28为TA VA；未知=360B TA ISA编码、544B缓冲内容、固件真实负载最低条件。

报告 mt-vgpu-guest/reports/r408-tasubmitta-no-construction.md。

