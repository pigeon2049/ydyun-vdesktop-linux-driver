# r405：真机适配缺口分析——从基础设施到真实可用的差距（离线）

> 轮次：r405（2026-10-09）。**本轮只做分析，不写代码，零硬件触碰。**
> 背景：r404 完成时基础设施就绪（TA marker 路径、R6 render context 真实化、3D 基础设施、R7 SyncPrimImportFD、安全网 T1/T2/T3、代码重构零警告）；用户授权继续推进真机适配，允许真实测试。

## 结论

**"真实可用"分级定义**（摩尔线程 vGPU guest，PowerVR DDK2 路径）：

| Tier | 定义 | 现状 | 备注 |
|---|---|---|---|
| T0 API 不报错 | UMD 全链路调用返回 0（建连→context→kick→destroy） | ✅ r144/r172 已达（123 调用零非零，CCB/TDM accept-and-log） | accept-and-log 不执行 GPU |
| T1 GPU 真实执行 | 真实 TA/3D 命令提交到固件并完成（非 marker） | ❌ 仅 marker（空包+opcode+fence+pid，固件回 0x100/0） | **本轮核心缺口** |
| T2 像素可验证 | 渲染结果回读，像素非平凡（triangle 可见） | ❌ 无回读基础设施（DDK2 路径） | 需 PMR→CPU 回读工具 |
| T3 UMD 集成 | 真实 DDK2 UMD 测试程序端到端跑通（含 GPU 执行） | ❌ r172/r174 UMD 跑起来但提交被拒/观察 | 需 T1 |
| T4 桌面/3D 栈 | Mesa/Vulkan/桌面加速 | ❌ 明确 out-of-scope | userspace README 已声明"尚无" |

**一句话**：T0 已达标；**T1（真实 GPU 执行）是当前最大缺口**；T2 是验证 T1 的手段；T3 是 T1 的自然延伸；T4 不在路线上。

**缺口优先级排序**：G1（真实 payload）> G3（3D 门控/活体）> G4（回读验证）> G2（UMD 集成）> G6（长尾）> G5（压力）。

---

## 缺口清单

### G1 真实 TA/3D payload（P0，最高优先级）

- **现状**：
  - TA marker 包 = `memset(0)` + `opcode 0x66 @0x0c` + `fence @0x48` + `pid @0x4c`（`mt_fw_ta_marker_command`，`kernel/mt_marker_fence.h:318`），固件回完成码 `0x100`。活体已验证多轮（r366/r372/r395/r397/r399），路径通但零绘制。
  - 3D marker 包同理（opcode `0x68`，完成码 0），**从未活体**（`MT_3D_SUBMIT_GATE`=0 关死，`static_assert` 钉住）。
  - 真实 TA 命令包布局**未知**：TA state stream、ISP 参数、region header 格式均未捕获。r174 捕获的是 TDM CCB（39/39 非零字节），不是 TA。
- **需要什么**：
  1. 真实 UMD TA kick 的 IN 参数捕获（r359 做了 `0x82:0xC` 活体 observer，有 IN 捕获能力）；
  2. UMD 侧 TA 命令缓冲内容捕获（VA→PMR→mmap 路径，r172/r174 已验证可行）；
  3. 命令包格式解析（对照 decompiled/ 与 Windows 驱动）；
  4. 合成 payload 或 UMD 捕获 replay，上机真实提交。
- **前置依赖**：无（可独立启动；r359 observer 基础设施已就绪）。
- **风险**：**高**——真实固件执行未知包可能 hang/firmware fault，需用户冷重启。必须走 pre-live 门禁 + 单模块 + 立即 teardown。
- **预计工作量**：3–5 轮（捕获→解析→构造→活体→调通）。

### G2 真实 UMD 集成（P1）

- **现状**：
  - r358：真实 UMD `.so`（SHA `b3058c02…34237b0`）`PVRSRVConnectionCreateDevice` 建连成功（conn=`0x25220fe0`，1ms），SRVCORE connect 收包，`GetSrvHandle` 指针形态确认。
  - r172：DDK2 全链 123 调用零非零（connect/devmemctx/render/syncprim/CCB 建销）；真实 `musa_blit_test` 发出首个真实 `0x89:0xa`（TDM），桥按设计回 `-25`（无 handler）。
  - r174：TDM `0x89:0xa` accept-and-log 上线，真实 CCB 39/39 非零字节与 fabricated 模型逐字节一致（仅 `+0x40` 2B 轮变）。
  - **但**：TA/3D 路径 UMD 从未走完——`0x82:0xC` 真实 UMD kick 未提交过真实负载。
- **需要什么**：G1 完成后，把真实 TA/3D 执行接到 UMD 调用链（UMD 发 kick → 桥真实执行 → fence 回填）。
- **前置依赖**：G1（真实 payload）。
- **风险**：中高（UMD 行为复杂，r174 已见"提交后退出行为不定"）。
- **预计工作量**：2–4 轮。

### G3 门控开启（P1，与 G1 并行可启动）

- **现状**：
  - `MT_3D_SUBMIT_GATE`=0（`kernel/mt_3d_submit.h:55`，`static_assert` 钉死 0）；`0x82:0x14` 仍为 accept-and-log observer（`mt_pvr_bridge.c:4439`），`mt_bridge_submit_3d_work` 已导出但 dispatch 未切换。
  - `MT_TA_VM_READY`：代码注释称"gate stays CLOSED: mapping validated (V1/V2)"（`mt_pvr_bridge.c:4274`）；R5 8 步映射流程的 V2（dummy 缓冲走映射）**计划过但未活体**（r375 中断于 oops，r376 重构后未补）。
  - 3D 路径**零活体**：从未在真机上提交过任何 3D 工作（marker 都没有）。
- **需要什么**：
  1. 3D marker 活体（开门控前的最小验证：`MT_3D_SUBMIT_GATE`=1 → 提交空包 → 固件回完成码 0）；
  2. TA VM 映射 V2 活体（dummy 缓冲走 8 步映射流程）；
  3. 确认无误后开门控。
- **前置依赖**：无（3D marker 活体可独立做，风险低于 G1）。
- **风险**：中（3D 首次真实提交；但 marker 包与 TA 同构，TA 已验证多轮，风险可控）。
- **预计工作量**：1–2 轮。

### G4 渲染结果回读验证（P2，验证 G1 的手段）

- **现状**：
  - DDK2 路径**无** framebuffer/screenshot/回读基础设施。
  - userspace 有 `mt-surface-check`（PPM 输出 1920×1080，23 GPU 填充+复制/读回核验），但那是旧 `mtvgpu` 路径（renderD129/130），**不是 DDK2 renderD128**。
  - PMR→CPU mmap 路径已验证可行（r172/r174 的 VA→PMR→mmap 关联）。
- **需要什么**：userspace 工具：打开 render target PMR → mmap → dump PPM → 像素校验（非零/期望图案）。
- **前置依赖**：G1（先有真实渲染，才有东西可读）。
- **风险**：低（纯 userspace，只读）。
- **预计工作量**：1–2 轮。

### G5 性能/稳定性（P3，T1 之后）

- **现状**：无长跑/压力测试；probe ref 基线 13 含 r389 旧泄漏（待用户冷重启清零）；多 context 并发 kick 未测（r391 V3 只测了双 context 隔离）；TA→TA 真异步等待未测（r366 诚实边界）。
- **需要什么**：多 context 并发 kick 压力、长时间运行 refcount 监控、fence 依赖链压力。
- **前置依赖**：G1/G2。
- **风险**：低（测试本身不改代码）。
- **预计工作量**：1–2 轮。

### G6 长尾缺口（P2–P3，按需）

| # | 缺口 | 现状 | 工作量 |
|---|---|---|---|
| 1 | `kick_pr=1` 未验证 | r364 V3 标记 TO-VALIDATE；TA+PR 诚实拒收（-EOPNOTSUPP） | 1 轮（需真实 PR 负载，依赖 G1） |
| 2 | `0x82:0x14`（MUSAKICKGFX5）仍 observer | r379 研究完成；r382 3D submit 落地但 dispatch 未切换 | 1 轮（G3 开门控时顺带） |
| 3 | sync-prim writeback 真实路径 | r386 ImportFD 只验 FD 存在性不解释 payload；ExportFD 0xB 未实现；`mt_ta_sync_update_apply` 遇非零 count 直接 -EOPNOTSUPP | 1–2 轮（T3 UMD 集成时需要） |
| 4 | TA→TA 真异步等待 | r366 诚实边界：未测 | 0.5 轮（G1 活体时顺带） |
| 5 | 16MB VM stride 待验证 | r387 设计标注 `0x70000000+idx*16MB` 待验证 | 0.5 轮（多 context 压力时验证） |

---

## 路线图建议（r406 起）

| 轮次 | 内容 | 对应缺口 | 性质 | 风险 |
|---|---|---|---|---|
| r406 | **3D marker 活体**：`MT_3D_SUBMIT_GATE`=1，提交 3D 空包，真机验证完成码 0 + fence | G3 | 活体 | 中 |
| r407 | **真实 TA payload 捕获**：UMD TA kick IN + TA 命令缓冲内容捕获（r359 observer 配方） | G1-①② | 活体 | 中 |
| r408 | **TA 命令包解析**：对照 decompiled/Windows 驱动，还原包布局 | G1-③ | 离线 | 低 |
| r409 | **真实 TA 提交活体**：合成 payload 或捕获 replay，固件真实执行 | G1-④ | 活体 | **高** |
| r410 | **回读验证工具**：PMR→mmap→PPM dump，像素校验 | G4 | 离线+活体 | 低 |
| r411 | **开 3D 门控 + 真实 3D**：`0x82:0x14` dispatch 切换，UMD 3D 路径 | G3/G6-2 | 活体 | 中高 |
| r412 | **UMD triangle 端到端**：真实 UMD 渲染调用全链路 | G2 | 活体 | 中高 |
| r413+ | 长尾收敛：kick_pr=1、sync-prim 真实路径、压力测试 | G6/G5 | 混合 | 低–中 |

**关键决策点**：
- r409 若真实 TA 执行 hang → 需用户冷重启，按安全协议这是预期内成本；
- r409 若固件拒收（类 r380）→ 回到 r408 重新解析包格式；
- T4（Mesa/Vulkan/桌面）明确不在路线上，不投入。

## 门禁

- `make -C mt-vgpu-guest check-offline`：待跑（本轮纯分析，预期全绿）。
- 本轮无代码改动，无新增测试需求。

## 诚实边界

- 纯离线分析，零硬件触碰；所有"现状"引用已有轮次报告，未做新的活体验证。
- G1 的"真实 TA 包布局未知"是基于 r172/r174 只捕获 TDM CCB 的事实；TA 包可能已有 decompiled/ 语料但未整理——r408 需先查。
- 工作量为估计（轮次），r409 的高风险可能导致 1–2 轮额外调试。
