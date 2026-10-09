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
