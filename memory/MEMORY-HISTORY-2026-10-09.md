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


## r409 (2026-10-09): 真实 TA 路径缺口确认（离线+实测）

**标准程序实测**：egltri_x11 运行 8s（0 次 musakickgfx2 dispatch）；glxinfo 显示 llvmpipe 软件渲染。Mesa 无 DDK2 UMD 后端，标准程序不走 0x82 桥，不可达。

**TA 缓冲结构**（离线反汇编 FUN_00169240）：40B（5 qwords）/72B（9 qwords）条目序列；render_ctx+0xb6 指针推进填充；TA state buffer；调用链 FUN_00169240→RGXKickTA→PrepareTA→SubmitTA（透传）。

**缺口**：当前 mt_ta_submit_build 仅发 80B marker（ta_params 存不发）；真实 TA 需 r410 扩展包构建器（含 360B VA）+ DMA VA 映射 + 条目语义。

**诚实边界**：360B 各 qword 语义未知（只知结构）；未做 TA 真实包活体；反汇编或有 decompiler artifact。

报告 mt-vgpu-guest/reports/r409-ta-path-gap-confirmed.md。

## r410 (2026-10-09): Windows 驱动挖掘——TA ISA 字段语义（离线）

**Windows 驱动**：/opt/MTT-driver-only/ 为纯二进制（24 DLL/SYS，无头文件/文档）；TA ISA 语义来自 Linux UMD 反汇编。

**TA 缓冲字段语义**（FUN_00169240，decompiled.c:43159）：40B（5 qwords）简单条目 / 72B（9 qwords）复杂条目；render_ctx+0xb6 指针推进。
- Q0 (local_90)：地址/标志，uVar16 位打包；或 0x48000000000 标志
- Q1 (uStack_88)：*(param_1+0x10)；byte7 标志位
- Q2 (local_80)：打包维度 ((w-1)&0x7fff)<<0x29 | ((h-1)&0x7fff)<<0x1a
- Q3 (uStack_78)：*(lVar29+8)；byte6 标志位
- Q4 (local_70)：维度乘积或打包维度
- Q5-Q8（复杂）：scissor/viewport 坐标打包

**RGXPrepareTA 回读验证**：psKickTA 字段从缓冲偏移 0x10/0x18/0x20/0x28/0x30/0x38/0x40/0x48/0x60 读取，确认布局。

**Linux 桥差异**：当前仅发 80B marker；缺 360B VA/size/DMA 映射/条目构造/psKickTA 结构。

**前置条件**：P1 确认固件包布局 + DMA 映射；P2 最小条目构造；P3 回读验证（可选）。

报告 mt-vgpu-guest/reports/r410-windows-ta-isa.md。


## r411 (2026-10-09): 真实 TA 包构造基础设施（离线）

- 新 `kernel/mt_ta_real.h`（99 行）：`MT_TA_REAL_PACKET` 门控默认 0、`MT_TA_CMD_BUFFER_BYTES=0x168`、40B 条目结构、`mt_ta_entry_simple_build()`（Q2 维度打包 [MEASURED] r410）。
- `mt_marker_fence.h` 集成：`mt_fw_ta_real_command()`（#if 门控内，VA @+0x28/size @+0x30 系 3D 类比 [INFERRED] TO-VALIDATE）；`mt_ta_submit_build` 门控分发 marker vs real。
- 新 `tests/ta/test_ta_real.py`（6 tests）：门控默认关、尺寸常量、条目构建器、real 命令被门控、marker 保留。
- 360B DMA/VA 映射未实现（需生产路径改动，独立前置）。
- 门禁 480+299 全绿，`make kernel` W=1 零警告（门控开/关双验证）；反向验证通过（门控篡改→FAIL）。
- 纯离线，零硬件触碰；门控关闭零行为变更。本地提交未 push。

## r412 (2026-10-09): 真实 TA 活体——实现完成，trial 阻塞

- 测试钩子 `pvr_cmd_ta_real_test`（桥 0x82:0xFE，未提交）：360B→BO[10]@4096（VM 已 seal，复用已映射 BO）→`mt_bridge_submit_ta_work` 真实路径（`MT_TA_REAL_PACKET=1` 测试构建）→等固件完成（0x100/超时/FAULT）。
- 用户态 `ta_real_test3`：INIT(2)→Connect→Create(0x12)→Test(0xFE)。
- **活体阻塞**：`pvr_session_acquire` 要求 `trial.pinned && trial.connected`；当前 trial 未建立。原版桥同样失败（`git stash` 验证），非本轮所致。r407 观察器不需要 trial 故当时未暴露。
- 编译零警告；pre-live T1/T2/T3 全过；无 oops/WARN/hang；测试修改已 revert（未提交）。
- 下一步 P0：诊断 trial 重建（probe 流程；可能需用户冷重启）。
---

## r414 (2026-10-09): 真实 TA 首次活体执行成功——固件 0x100 完成

- 补加 `case 0xFE:` 分发（r413 卡点：r404 后空白字符 `\tcase…:\t\t\t` exact-match 解决）+ `pvr_cmd_ta_real_test` 钩子（r412 hook.c 原样，169 行）于 `pvr_dispatch_rgxta3d` 之前；`make kernel` W=1 零警告。
- Pre-live T1/T2/T3 全过（10 tests）；一次桥加载→单发→`safe_rmmod.sh` 卸载（ref=0）；`mt_guest_probe` 未动。
- 活体：`INIT(2)→Connect→Create(0x12, handle=0x1000)→Test(0xFE)` → `status=0`，dmesg `COMPLETED (0x100) wire=1`，提交到完成 **219µs**（真实执行）。
- DM 布局验证：VA @+0x28/size @+0x30 由 r411 [INFERRED] 转 **[MEASURED]**；40B 简单条目（64×64 dummy）被固件接受。
- 无 oops/WARN/hang；测试钩子未提交（工作区 UNCOMMITTED）；门禁 480+299 全绿。
- 诚实边界：dummy 构造语义未深挖；T2 回读仍缺；生产路径仍 marker。

## r415 (2026-10-09): 真实 TA 路径产品化——DM 布局固化 + mt_ta_submit_real() 落地

r415（离线）：DM 布局 VA @+0x28/size @+0x30 由 [INFERRED] 转 [MEASURED]（r414 活体 0x100/219us）；80B 包布局文档化。新生产函数 `mt_ta_submit_real()`（`kernel/recovery/mt_pvr_bridge.c`，`#if MT_TA_REAL_PACKET` 内）：r414 测试钩子重构为参数化 API（`struct mt_ta_real_request`{h_render_context,width,height,n_entries}），固定 64x64 hardcode 已移除，异步返 fence（调用方自行等待）；`mt_ta_real_buffer_build()` 纯函数（n 个 40B 条目）。BO[10]@4096 复用评估通过（VM seal 后无法新增绑定，r414 已验证；长期 fix 为 create 时专用 BO）。门控开启流程文档化（5 前置+开启步骤+回滚，marker 路径不受影响）。`0x82:0xFE` 钩子确认不在生产代码（从未提交）。`tests/ta/test_ta_real.py` +8（共 14）。门禁 488+299 全绿，`make kernel` W=1 零警告（门控开/关双路径），反向验证通过（门控置 1 → FAIL）。T2 白名单曾因注释引用宏名告警，已改为裸 0x66。本地提交未 push。


## r419 (2026-10-09): Q0 是纯 flags、地址在 Q1（离线反汇编）

r419（离线反汇编，零硬件）：r418 活体 Q0=`va|0x48000000000` 致固件超时，r414 Q0=0 曾 219us 成功。深挖 FUN_00169240：Q0 初始构造 `(sVar10<<4)<<48|(1<<61)` 无地址位（:44213）；Path B `uVar15|(prev&mask)|0x48000000000` 纯 flags carry-forward（:44317）；Q1 低 48 位=`*(param_1+0x10)` 才是目标地址（:44300/44321）。**核心结论：Q0 是纯 flags/control 字，零地址位；48 位目标地址在 Q1。** r418 把 VA OR 进 Q0 污染 flags。修正 `mt_ta_entry_simple_set_target()`：Q0=`0x48000000000`（flags only），Q1=`va & 0xFFFFFFFFFFFF`。位域：bits 39/42（0x48000000000，纠正 r410 的 43/46 笔误）、29/30 条件位、43-50 local_c4。门禁全绿，kernel 零警告。诚实边界：Q1=render target 为推断，待活体验。



## r418 (2026-10-09): 双门控回读活体——路径通、ABI bug 修复、固件超时

r418（最高风险轮，活体）：双门控构建（MT_TA_READBACK_DEBUG=1 + MT_TA_REAL_PACKET=1，intentional static_assert 临时中和，已还原）`make kernel` W=1 零警告；pre-live T1/T2/T3 全过；`insmod` 新桥后跑 `mt-ta-readback`——首轮 `pvr_in` 报 -EINVAL，dmesg 调试定位到 ABI bug：`struct mt_pvr_ta_readback_in` 内核侧未 packed（24B）vs userspace packed（20B），r416/r417 离线测试未捕获；修复 1 行（`__attribute__((packed))`）后 0xFD 全路径执行，`mt_ta_submit_real` 成功、fence 分配，但固件 5s 超时（ETIMEDOUT，submitted-but-ignored）；r414 同结构 TA（Q0=0）219µs 完成，本轮 Q0=`va|0x48000000000`（[INFERRED]）后超时——Q0 编码很可能不对，不做盲探；12th target BO 活体绑定确认（0x7b000000，16KB）；pending TA fence 致 bridge ref=1，`safe_rmmod.sh` 正确拒绝未强卸，待用户冷重启；门控/断言已 revert，源码树仅保留 packed 修复；门禁 522+625 全绿；本地提交未 push。诚实边界：Q0 仍 [INFERRED] 待离线深挖；像素未验证，T2 仍 open；生产零改动。

## r417 (2026-10-09): 回读路径测试加固——+19/+326 测试，两处门控 latent build break 修复

r417（离线，零硬件）：用户指示"稳妥推进 先加测试"。`tests/ta/test_ta_readback.py` +19（12th BO 生命周期 6：create 绑定位置/槽位 11 无冲突/destroy 逆序/bind 失败无泄漏/rollback 复用 destroy/VA 槽位表达式；0xFD 参数校验 6：坏 ctx→-EINVAL/未就绪→-ENODEV/参数透传/门控一致性/关门→-ENOTTY/前向声明；target_va 4：空指针/n_entries 越界/透传/staging BO 检查；像素分析 3：头文件存在/工具引用/ENOTTY 双门控提示）。`tests/c/pvr_bridge_core_test.c` +4 函数（+326 checks：entry 构建校验/Q0 位打包/buffer target_va/像素分析单元测试，含 black quirk/alpha 忽略/16 色 cap）。测试发现两处真实 latent build break 并修复：(1) 0xFD dispatch case 门控仅 DEBUG，handler 需 DEBUG&&REAL——(1,0) 报 implicit declaration，已改双门控；(2) `pvr_cmd_ta_readback` 调用 `mt_ta_submit_real` 早于其定义且无前向声明——(1,1) 历史从未编译成功，已补声明。新建 `userspace/ta_readback_analyze.h`（像素逻辑提取，行为锁定 r416）；`mt-ta-readback.c` 改用；ENOTTY 提示注明双门控。r415 的 2 个测试改锚定 `__maybe_unused` 定义。门禁 522+625 全绿，`make kernel` W=1 零警告；反向验证 4 项（TDD 红→绿、Q0 OR→XOR 捕获、像素 shift 破坏捕获、门控组合构建实证）。本地提交未 push。诚实边界：Q0 flag 仍 [INFERRED]；(1,*) 构建验证临时中和 intentional static_assert（已还原）；活体验证未做。

## r420 (2026-10-09): Q0/Q1 修正测试加固 + T4 纯净性门禁（离线）

r420（离线，零硬件）：用户指示"继续 加更多测试和门禁"。新增 21 Python 测试：`tests/ta/test_q0_purity.py`（T4 门禁，3 tests：Q0 禁止 OR/address 源码扫描、常量仅 bits 39/42、Q1 必须接 VA）、`tests/ta/test_q0_q1_bitfields.py`（15 tests：Q0 flag 位独立、低 32 位禁区、Q1 48 位 mask 边界、三态历史 r414/r418/r419）、`tests/ta/test_ta_real.py::TestTaDmLayoutUsage`（3 tests：常量被使用、禁硬编码 0x28/0x30、注释 [MEASURED]）。修复 1 处 stale 文档：`mt_marker_fence.h` 的 [INFERRED] 注释更新为 [MEASURED]（r414）。T4 反向验证：注入 r418 污染 → FAIL（定位行号）；还原 → 绿。门禁 543+299 全绿（Python，1 skipped）、630 C 全绿；`make kernel` W=1 零警告。本地提交未 push。诚实边界：Q0 flag 语义（除 29/30/39/42）仍未知；Q1=target 待活体验。

## r421 (2026-10-09): Q0 修正后活体——提交成功但固件仍超时（Q1 待深挖）

r421（最高风险活体）：双门控测试构建（MT_TA_REAL_PACKET=1 + MT_TA_READBACK_DEBUG=1，static_assert 临时中和，事后 revert；W=1 零警告）。pre-live T1/T2/T3/T4 全过（13 tests）。冷重启后 probe 全参数链加载，trial 重建成功（connect=0 pinned=1）；双门控桥加载，/dev/dri/renderD128 就绪。mt-ta-readback 全链路执行：context 0x1000 创建，11 BO + 12th target BO（va=0x7b000000 bytes=16384）绑定成功；0xFD 提交（Q0=0x48000000000 flags-only [MEASURED]，Q1=0x7b000000 [INFERRED]）→ fence 分配 → 5s 无完成事件（-ETIMEDOUT，submitted-but-ignored）。对比 r414（Q0=0/Q1=0，219us 完成）：修正后的 Q0/Q1 编码仍未被固件接受。dmesg 零 WARN/BUG/Oops；pending fence 致 bridge ref=1，safe_rmmod.sh 正确拒绝（未用 -f），待用户冷重启。门禁 543+299 全绿；kernel W=1 零警告；源码已 revert（仅保留 committed 状态），工作区干净。诚实边界：Q1=target_va 仍 [INFERRED]，本次活体未能将其提升为 [MEASURED]——Q1 编码可能仍不对，或 TA 条目其他字段（Q2/Q3/Q4）/DM 包布局另有问题；T2 像素回读仍 open；下一步 P0：离线深挖 Q1/target 语义（RGXPrepareTA 回读偏移 0x10/0x18/.../0x60 的对应关系）。

## r422 (2026-10-09): TA 缓冲 Header+Entries 双区——Entry 写错位置致 r421 超时（离线反汇编）

r422（纯离线，零硬件）：r421 超时根因定位。反汇编证实：RGXSubmitTA（FUN_001796b0，decompiled.c:54365）从 TA_buf+0x10/0x18/0x20/0x28/0x30/0x38/0x40/0x48/0x60 回读 9 qword 到 psKickTA [MEASURED]；RGXPrepareTA（FUN_00178800）向 TA_buf+0x10/0x28/0x30/0x68/0x120/0x140 写 Header，覆盖 0x00-0x160 [MEASURED]。我方 `mt_ta_real_buffer_build()` 把 40B Entry 写在 `buf+0x00`（mt_ta_real.h:178-179），Q2/Q3/Q4（0x10-0x27）恰好覆盖 Header 的 0x10/0x18/0x20 字段 → 固件经 psKickTA 读到垃圾 Header → 挂起超时。r414 全零缓冲=空 Header=无工作，故 219us 成功；r421 非零 Entry=污染 Header，故超时。Q1=target_va（Entry 内）未被证伪，但 Entry 位置错误是更直接原因。Entries 真实容器未知（544B 缓冲为推断）。r423 前置：P0 确定 Entries 容器或 Header-only 测试（仅设 TA_buf+0x10=target_va）；P1 改 `mt_ta_real_buffer_build()` 不再写 buf+0；门禁 T5（Header 完整性）待加。诚实边界：Ghidra 伪 C 或有 artifact；本轮无代码变更。


## r425 (2026-10-09): Header-only 活体——固件仍超时（Header-only 不充分）

r425（最高风险活体）：r423 Header-only 方案首次活体验证。双门控测试构建（MT_TA_REAL_PACKET=1 + MT_TA_READBACK_DEBUG=1，static_assert 临时中和；userspace n_entries 1→0 临时；事后全部 revert；W=1 零警告）。pre-live T1-T5 全过（550+1416）。冷重启后 probe 全参数链加载，trial 重建成功（connect=0 pinned=1）；双门控桥加载，/dev/dri/renderD128 就绪。mt-ta-readback 全链路执行：context 0x1000，12th target BO（va=0x7b000000）绑定成功；0xFD 提交（Header-only：buf+0x10=target_va，其余零，n_entries=0 被接受）→ fence 分配 → 5s 无完成（-ETIMEDOUT）。结论：Header-only 不充分——r422 的"Entry 污染 Header"是真实 bug（T5 已拦截）但不是超时的完整解释；r414 全零="无工作"快路径。固件很可能要求 +0x10 指向 render-target 元数据结构（非原始像素 BO）及/或其他 Header 字段有效。pending fence 致 bridge ref=1，safe_rmmod.sh 正确拒绝（未用 -f），待用户冷重启（第 5 次）。dmesg 零 WARN/BUG/Oops。门禁 550+1416 全绿。诚实边界：Header-only 仍 [INFERRED] 未 [MEASURED]；下一步必须离线确定 +0x10 真实语义与必需 Header 字段；不再做无离线依据的活体试探。

## r426 (2026-10-09): +0x10 指向 render-target 元数据结构——真实 Header 需 UMD 上下文状态（离线反汇编）

- FUN_00178800（RGXPrepareTA）完整写入清单 [MEASURED]：+0x10=*(render_ctx+idx*0xD0+0x38)（per-buffer 描述符数组，非原始像素 BO）；+0x28=*(render_ctx+0x440)、+0x30=*(render_ctx+0x448)；+0x68 布尔标志；+0x120 位打包；+0x50/0x58 经 FUN_00184220。
- +0x10 语义：render-target 元数据结构的设备地址，由 UMD 在 render context 创建时分配并初始化到描述符数组+0x38 处。PowerVR render target 是固件可解析的结构（含颜色/深度缓冲地址、tile 配置等），非裸像素缓冲。
- FUN_0017d890（psKickTA 构建）[MEASURED]：[1]=*(TA_buf+0x10)、[4]=magic 0x3089705f3089705f（UMD 写入）、[3]=*(TA_buf+0x28)、[10]=*(TA_buf+0x30)。
- r425 超时根因：16KB 像素 BO 非有效元数据结构，固件按结构布局解析垃圾→挂起。r414 全零=psKickTA[1]==0→"无工作"快路径。
- 真实 Header 大多数字段指向 UMD 上下文内部状态，无法从零构造。r427 前置：捕获真实 UMD TA Header 回放（推荐）或逆向 render context 初始化；P0 完成前不得活体。
- 纯离线零硬件；门禁待跑（无代码变更）。

## r427 (2026-10-09): Render Target 元数据结构逆向——psKickTA 全布局已测，结构本体为固件私有（离线反汇编）

- psKickTA 完整 18-qword 布局 [MEASURED]（FUN_0017d890，decompiled.c:54347）：[0]=RTData entry+0x08、[1]=TA_buf+0x10=RTData entry+0x00（render target VA）、[2]=*(ctx+0x3d8+idx*8)、[3]=TA_buf+0x28=*(ctx+0x440)、[4]=magic 0x3089705f3089705f（UMD 写入）、[5]=*(ctx+0x10)、[10]=TA_buf+0x30=*(ctx+0x448)、[12]=0x10 常量。
- RTData 条目 0xD0 字节字段表 [MEASURED]：+0x00→psKickTA[1]、+0x08→psKickTA[0]、+0x48/+0x50 sync、+0xC8 缓存、+0x118/+0x120 sync；数组基址=lVar3+0x38，条目=lVar3+0x38+idx*0xD0。
- Render context state 关键偏移 [MEASURED]：+0x24 buffer 索引、+0x440→TA_buf+0x28、+0x448→TA_buf+0x30。
- RGXAddRenderTarget 创建流程：RGX_RT_ALLOCS、parameter memory、MLIST、VHEAP；RTDataSet 分配者在 UMD 之外（未找到）。
- 结论：psKickTA[1] 指向的结构本体为固件私有——UMD 只透传 VA，布局无法从 UMD 反汇编确定；方案 B 已达边界。r428 前置：选项 A（捕获真实 UMD Header）或选项 C（3D 路径验证 T2）；P0 完成前不得活体。
- 纯离线零硬件；门禁 550+1416 全绿；无代码变更。

## r428 (2026-10-09): UMD 环境取证——无可运行 Linux vguest，真实 TA Header 走 Windows 驱动静态提取（离线调查）

- 用户澄清：Linux 端没有 vguest；`/opt/MTT-driver-only/` 为真实可工作的 Windows guest 驱动。全 deb/src 取证：mtgpu-1.0.0=固件+dkms 源码、mtml-1.8.2 仅 `libmtml.so`、dkms 包无 .so——**无 Linux 图形 UMD .so**（[MEASURED]）。
- `decompiled/linux-legacy-umd-5.2.0/` = Ghidra 反汇编 `libsrv_um_MUSA.so.1.0.0`（host-side 服务库，SHA-256 `b3058c02…34237b0`），非 vguest。
- `mtdxum64.dll` 实证为 DX10/11 UMD：导出 `OpenAdapter`/`OpenAdapter10`/`OpenAdapter10_2`/`MtDxExtGetInterfaceImpl`（MT 特有）；经 `D3DDDIEscapeCb` 提交；TA 命名函数无（本地名 strip）。
- 可行性：Wine 不可行（UMD 需 KMD .sys，vGPU 被占用）；Windows VM 不可行（需 host 配合）；**选项 A 落点=静态提取**（语料完备，Ghidra 工程可重开）。
- 弹药：`0x168` 在 mtdxum64.dll 出现 151 次（行为锚点）；`RGXAddRenderTargetDDK2`（decompiled.c:50202）分配 `"MLIST"`/`"RgnHeader"` 固件可见结构；修正 r427"UMD 只透传"为过于绝对。
- r429 工作包：mtdxum64.dll 以 0x168 定位 TA Header builder → 对比 `RGXPrepareTA` → `RGXAddRenderTargetDDK2` 全量 → mtkm64.sys KMD 侧逻辑。
- 零硬件触碰，纯离线；无代码改动。


## r429 (2026-10-09): Windows 驱动 TA 提交结构提取——D3D11 UMD 用 0x78 字节 kick，非 360B Header（离线反汇编）

- mtdxum64.dll（DX10/11 UMD）构建 0x78 字节 kick 条目（FUN_180224220 @392802、FUN_1802411e0 @412745），magic 0x3089705f3089705f 在 **[1]**（Linux UMD psKickTA[4]），经 D3DDDIEscapeCb 提交；调用者在 FUN_18021cf50（0x78 步长数组，清零 15 qword 后逐条填充）。
- kick 字段表 [MEASURED]：[0]=VA、[2]=0x100000000、[3]=2、[8]=像素格式映射、[9]=维度打包 ((h-1)<<16|(w-1))、[10]=VA&~0xf、[0xb]=VA>>4。
- 0x168 的 151 次命中去噪：~140 vtable 偏移 + ~8 C++ 对象大小均为噪声；**Windows 侧无 360B TA Header 分配**——D3D11 抽象层不同。
- MTT 特有 delta：kick 布局与 Linux 不同，但 render-target 元数据仍为固件私有（r427 结论不受影响）。
- 结论：**360B TA Header 为 Linux UMD 特有**；同一固件接受 D3D11 kick 与 Linux TA Header——提交格式由 UMD/KMD 协商。
- r430 前置：Linux 侧 RGXAddRenderTargetDDK2 的 MLIST/RgnHeader 布局（render-target 元数据最佳线索）。
- 零硬件触碰，纯离线；无代码改动。


## r430 (2026-10-09): TA Header +0x10 = RgnHeader device VA——3-hop 链实证；纠正 r428 文件归属（离线反汇编）

- **核心结论**：TA Header `+0x10` = **RgnHeader device VA**（[MEASURED] 三跳链）：
  1. `RGXAddRenderTarget:49312`：`local_5b0[1] = local_6d8`（RgnHeader dev VA → VA 表）
  2. `SetupRTDataSet:48867`：`*(RTDataSet+0x38) = *(param_4+8)` = local_5b0[1] → RTData entry+0x00
  3. `RGXPrepareTA:52144`：`*(TA_buf+0x10) = RTData entry+0x00`
  → `psKickTA[1]` = RgnHeader VA；固件解析 RgnHeader 获 tile 布局。r425 超时（+0x10=原始像素 BO）彻底解释。
- **RgnHeader** [MEASURED]：size = `numRT × round_up(tiles×0x40,64)`（64×64 → 0x100B）；
  UMD 经 `InitRegionHeaderBuffer` 预填全 `0xFFFFFFFF`（heap=0x133 路径）；`DevmemAllocateAndMap` 设备可见。
- **MLIST** [MEASURED]：size = `numRT × 0x4a000`（config+0x5c，另有 0x72000 变体）；固件写入，不预填。
- **纠正 r428**：`RGXAddRenderTargetDDK2` = `linux-legacy-umd-5.2.0/decompiled.c:50203`（270 行），
  非 mtdxum64.dll（全语料库 grep：MLIST/RgnHeader 仅 Linux UMD 有；mtdxum64.dll:50202 是 C++ 容器初始化函数）。
- **最小有效 TA Header**：+0x10=RgnHeader VA（分配 0x100B 填 0xFF）；+0x28/+0x30=[UNKNOWN]；
  +0x68=0；其余 0。r431 P0：Linux guest 实现 RgnHeader 分配+初始化。
- 门禁 `check-offline` 全绿；零硬件触碰，纯离线，无代码变更。


## r431 (2026-10-09): RgnHeader 13th BO 实现——TA Header +0x10 指向 RgnHeader（离线）

- **核心结论**：render context 新增第 13 个 BO（RgnHeader）：64×64 → 0x100B，
  预填全 `0xFFFFFFFF`（[MEASURED] r430，InitRegionHeaderBuffer），绑定 VA slot 12
 （0x7c000000）。`TA_buf+0x10` 改取 `rgnheader_va`（不再是 16KB 像素 BO）；
  r425 超时（像素当 region header 解析）此路径不再重演。
- **实现**：`mt_ta_real.h` 新增常量 + `mt_ta_rgnheader_size(w,h)`（round_up(tiles×0x40,64)）；
  `mt_render_context.h` struct += 3 字段（sizeof 1720→1824）；`mt_pvr_bridge.c`：
  create 分配+0xFF 初始化+slot 12 绑定，destroy 释放，0x82:0xFD 要求 rgnheader_ready。
- **测试**：C 新增 size 公式（64×64→0x100、128×128→0x400、65×65→0x240）+
  全 1 初始化验证；Python layout 测试更新偏移。
- 门禁 `check-offline` **550 Python + 1490 C 全绿**；`make kernel` W=1 **零警告**。
- `+0x28`/`+0x30` 仍 [UNKNOWN]（置零）；活体验收延至 r432。零硬件触碰，纯离线。

## r432 (2026-10-09): RgnHeader 活体——固件仍超时，RgnHeader 非充分条件（最高风险）

- 核心结论：RgnHeader BO 正常创建绑定（va=0x7c000000 bytes=4096，0xFF 预填），
  TA Header +0x10 正确指向 RgnHeader，但固件 5s 内仍无完成（-ETIMEDOUT）。
  RgnHeader 是必要非充分条件；+0x28/+0x30 或 RgnHeader 内容语义仍有缺失。
- 活体：双门控测试构建（W=1 零警告）；T1-T5 全过（554 Python + 1490 C）；
  第 5 次冷重启后 trial 重建（connect=0 pinned=1）；0xFD 提交走通（fence 已分配）；
  仅完成事件缺失。dmesg 零 WARN/BUG/Oops。
- Teardown：pending fence 导致 bridge ref=1，safe_rmmod.sh 正确拒绝（未用 -f）；
  待用户第 6 次冷重启。源码已 revert，默认门控重建零警告，工作区干净。
- 对比表：r414 全零→219us（无工作快路径）；r425 +0x10=像素 BO→超时；
  r432 +0x10=RgnHeader→仍超时。RgnHeader [INFERRED] 未升 [MEASURED]（证伪性证据）。
- 下一步必须离线：+0x28/+0x30 语义与 RgnHeader per-dword 要求；
  不再做无依据活体试探。零 rmmod -f、零自行重启。

## r433 (2026-10-09): RgnHeader 填充是 0x00000001 非 0xFFFFFFFF;+0x28/+0x30 链条追踪（离线）

- **核心纠正**：`InitRegionHeaderBuffer` 逐 dword 写整数 `1` (`*local_690[0] = 1`,
  `undefined4*` [MEASURED])，**不是** `0xFFFFFFFF`。r430/r431 的 "0xFFFFFFFF"
  结论错误；r431 `MT_TA_RGNHEADER_INIT_DWORD 0xFFFFFFFFU` + `memset(0xFF)` 与 UMD
  行为不符。**r434 P0: 改为逐 dword 写 `0x00000001`。**
- **+0x28/+0x30 链条** [MEASURED, 终端 UNKNOWN]：
  `TA_buf+0x28/+0x30` ← `TA_state+0x1cc/+0x1ce` ← `RTDataSet+0x440/+0x448`
  ← `*(local_5b0+0x68)`/`*(local_5b0+0x80)` (RGXAddRenderTarget)。
  终端值因 Ghidra 数组定界 [UNKNOWN]；MLIST VA 为首要候选 [INFERRED]。
- **MLIST** [MEASURED]：0x4a000B (64x64)，firmware-written，UMD 不预填；
  VA (`local_558`) 分配后未见引用；未出现在 TA Header/psKickTA 中。
  TA kick 可能不需要 MLIST VA，或经 +0x28/+0x30 传递。
- **Mcg patching**：多 RT 时填充后 patch `[2]/[3]` (VA 低/高 32 位，stride 0x40 dwords)；
  单 RT (我方) 无 patching，仅 fill。
- 门禁 `check-offline` 全绿；`make kernel` 未跑（无代码变更）。
  零硬件触碰，纯离线。

## r434 (2026-10-09): RgnHeader 填充修正为逐 dword 写 0x00000001（离线）

- 落地 r433 纠正：`MT_TA_RGNHEADER_INIT_DWORD` 由 `0xFFFFFFFFU` → `0x1U`；
  `mt_render_context_create` 删除 `memset(rgn_init, 0xFF, ...)`，
  改为逐 dword 循环写 `1`（复用 `u32 i`，[MEASURED] r433）。
- 测试同步：`test_ta_rgnheader_init_pattern` 模拟逐 dword 写 1，
  断言 `== 0x1U` 且 `!= 0xFFFFFFFFU`（防 r431 重演）；
  `test_rgnheader_init_all_ones` 更新为 assertIn dword 循环 +
  assertNotIn memset 0xFF（首轮即精确拦截旧行为）。
- 反向验证：注入 `memset 0xFF` → 精确 FAIL；还原后全绿。
- 门禁 `check-offline` **554 Python + 1491 C 全绿**；
  `make kernel` W=1 **零警告**。
- 诚实边界：RgnHeader 语义仍 [INFERRED]；0x00000001 填充尚未活体验收
  （r435+，待用户冷重启）。零硬件触碰，纯离线。

---

## r435 (2026-10-09): 第 6 次冷重启未发生，停止活体（只读检查）

- 只读核查 [MEASURED]：启动 ~16:20:46 CST（dmesg -T 反推：17:43:24 − 4958s）；
  r432 活体 17:45:43（render context READY，13th rgnheader BO bound）在启动之后——
  **第 6 次冷重启未发生**。
- `mt_pvr_bridge` ref=1（r432 pending fence 遗留，safe_rmmod 已拒绝）；
  `mt_guest_probe` ref=1（正常）；残留完整 render context 未 teardown。
- 任务停止条件命中，**未执行任何活体操作**：未构建双门控、未重载 bridge、
  未跑 `mt-ta-readback`；未触碰残留会话。
- 门禁 `check-offline` **554 Python + 1491 C 全绿**；`make kernel` W=1 零警告。
- 证据 `mt-vgpu-guest/build/traces/r435/dmesg-r435.txt`（0600）。
- 下一步：用户执行第 6 次冷重启后重验（uptime/lsmod/dmesg），方可 r436 活体。
- 诚实边界：启动时间反推 ±2s；残留会话归属 r432 为 [INFERRED] 高置信；
  零硬件触碰；生产代码零变更。

## r436 (2026-10-09): RgnHeader fill-1 live -- firmware still 5s timeout (highest-risk)

- 6th cold reboot live: dual-gate build (W=1 zero warnings), T1-T5 pass
  (554 Python + 1491 C).
- Trial rebuild lesson: runtime_context=0 clean trial (pinned=0) caused bridge
  0x82:0x12 -ENODEV -- pvr_session_acquire() requires trial.pinned AND
  trial.connected; reloaded probe with runtime_context=1 ->
  pinned=1 connected=1 (Guest/FW 2/2, matches r432 session state).
- Full chain: connect -> ctx 0x1000 -> 13th RgnHeader BO (va=0x7c000000,
  per-dword fill 1, dmesg confirms binding) -> 0xFD submit (buf+0x10=0x7c000000)
  -> fence allocated -> 5s timeout (errno=110).
- r433/r434 fill correction FALSIFIED as root cause by live evidence
  (0xFF -> 1 did not change behavior).
- Compare: r414 (all-zero, 219us no-work) / r425 (pixel BO, timeout) /
  r432 (RgnHeader fill 0xFF, timeout) / r436 (RgnHeader fill 1, timeout).
- Teardown: pending fence -> bridge ref=1, safe_rmmod.sh correctly refused;
  dmesg zero WARN/BUG/Oops; awaiting user 7th cold reboot.
- Source reverted, default rebuild W=1 zero warnings, tree clean.
- Honest boundary: RgnHeader still INFERRED; next MUST be offline on
  +0x28/+0x30 (MLIST VA candidate). No more live probing without basis.

## r437 (2026-10-09): +0x28/+0x30 单 RT 恒为 0，MLIST VA 不进 kick 路径（离线反汇编）

- 终局结论 [MEASURED]：`TA_buf+0x28`/`+0x30` 在单 RT 下恒为 0——
  `local_5b0+0x68`/`+0x80` 即栈局部 `local_548`/`local_530`
  （rbp 偏移恒等式：rbp-0x5b0+0x68=rbp-0x548），在
  `RGXAddRenderTarget:49349-49350`（`local_62c < 2` 单 RT 分支）赋 0，
  全函数 49000–50000 无其他赋值点；我方置零与 UMD 完全一致，
  r432/r436 超时与此二字段无关。
- MLIST VA（`local_558`，decompiled.c:49222 唯一赋值）在 49000–49900
  零读取——分配后即丢弃，不进 TA Header/psKickTA 构建；
  r433 的"MLIST VA 首要候选 [INFERRED]"被证伪。
- psKickTA[3]/[10] = 0（单 RT）。
- r438 前置（离线）：+0x68 布尔（UMD 写 `(*param_2&3)==3`，我方写 0）、
  单 RT 下 RGXPrepareTA 完整写入清单逐项对照、RgnHeader 内容、DM 包本身；
  P0 完成前不得活体。
- 门禁 `check-offline` 全绿（554 Python + 1491 C）；无代码变更，未跑
  `make kernel`；零硬件触碰，纯离线。

## r438 (2026-10-09): +0x68 布尔 + 单 RT 完整写入对照，新嫌疑 +0x50/+0x58（离线反汇编）

- `+0x68` [MEASURED 表达式，UNKNOWN 取值]：`*(uint *)(TA_buf+0x68) =
  (uint)((*param_2 & 3) == 3)`（`RGXPrepareTA:52136`，52357 同式）；
  `param_2` = psKickTA（`RGXKickTA`/`RGXKickGfx` 透传，顶层导出函数），
  `*param_2` 为其 flags dword；bit0+bit1 全置才写 1。flags 由 DDK 层设置，
  UMD 语料无构造点——真实提交中取值无法确定；我方写 0。
- 单 RT（`*(lVar6+0x18)==1`）完整写入清单 [MEASURED] vs 我方 Header-only：
  +0x10 RgnHeader VA ✅；+0x28/+0x30 = 0 ✅（r437）；**+0x50/+0x58 =
  `((psKickTA[3 or 4]+0x3f>>6)&0x3f)<<48`（FUN_00184220 tile 打包，
  (x+63)/64 tile 数语义 [INFERRED]，公式/bit48-53 [MEASURED]）❌ 我方 0
  = "0 tiles"（P0 嫌疑）**；+0x68 布尔 ⚠️ P1；+0x120 flags 位打包 ⚠️ P2；
  +0x138–+0x160 feature 条件 ?；+0x78 起多 RT 块单 RT 跳过 ✅。
- r439 前置（P0）：`mt_ta_real_buffer_build()` 新增 +0x50/+0x58 tile 打包
  （w/h 近似，标 [INFERRED]），T5 白名单同步，新增 C/Python 测试；
  +0x68/+0x120 暂保持 0；P0 实现+门禁全绿后方可活体。
- 门禁 `check-offline` 全绿；无代码变更，未跑 `make kernel`；
  零硬件触碰，纯离线。


## r439 (2026-10-09): TA Header +0x50/+0x58 tile 打包实现（离线）

- r438 P0 落地：`mt_ta_tile_pack(x) = (((x+0x3f)>>6)&0x3f)<<48`（[MEASURED] 公式，
  tile 语义 [INFERRED]）；`mt_ta_real_buffer_build()` 新增 `+0x50=w` 打包、
  `+0x58=h` 打包（w/h 近似 psKickTA[3]/[4] [INFERRED]）；64×64→`0x0001000000000000`。
- T5 白名单扩展：仅 `MT_TA_BUF_HDR_TARGET_VA`/`_TILE_PACK_X`/`_Y` 可写；
  新增 `test_header_tile_pack_writes`/`test_tile_pack_constants`/
  `test_tile_pack_helper_defined`。
- C：新增 `test_ta_tile_pack`（10 checks，含 4096→0 的 6-bit 回绕）；
  `test_ta_real_buffer_build_target` 同步（+0x50/+0x58 非零断言、幂等循环
  跳过三处写入区；动态 1491→783 系冗余循环合并，覆盖未减）。
- 反向验证：tile 写改 0→`test_header_tile_pack_writes` 精确 FAIL；还原→绿。
- 门禁 `check-offline` 557+783 全绿；`make kernel` W=1 零警告；零硬件触碰。
- 诚实边界：tile 语义 [INFERRED]；`+0x68`/`+0x120` 仍 0（r438 P1/P2）；活体待定。
