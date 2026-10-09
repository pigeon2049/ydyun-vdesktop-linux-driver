# r412：真实 TA 活体——实现完成，试验证明 trial 未建立（阻塞）

> 轮次：r412（2026-10-09）。**最高风险轮。实现完成，活体被 trial 状态阻塞。**
> 背景：r411 基础设施（门控默认关、40B 条目构建器）；用户指示"继续"，已授权真实测试。

## 结论

**360B DMA 映射与真实 TA 提交路径实现完成，编译零警告；活体单发被 trial 未建立阻塞（`pvr_session_acquire` 返回 NULL）。**

- 测试钩子 `pvr_cmd_ta_real_test`（桥 0x82:0xFE，未提交）：分配 360B、填充 40B 条目（64×64）、写入 BO[10]@4096、经 `mt_bridge_submit_ta_work` 真实路径提交、等待固件完成。
- 门控 `MT_TA_REAL_PACKET=1`（测试构建；生产代码保持 0）。
- **阻塞**：`pvr_session_acquire` 要求 `g->trial.pinned && g->trial.connected`；当前 trial 未建立。**原版桥（无测试修改）同样失败**，证明非本轮改动所致。
- 系统稳定：无 oops/WARN/hang；测试桥已加载（ref=0）；源码测试修改未提交。

## 实现细节

### 设计决策

| 决策 | 理由 |
|---|---|
| BO[10]@4096 复用 | per-context VM 已 seal，`mt_gpu_vm_bind_many` 返回 `-EBUSY`；BO[10]（8192B）已映射于 `vas[10]`，offset 4096 安全 |
| kmalloc 360B 源缓冲 | 任务要求 `dma_alloc_coherent`；实际经 `pvr_translator_bo_write` 写入 BO（已验证的固件可见路径，r389）；BO 内存即 DMA 映射 |
| 测试 ioctl 0x82:0xFE | 复用 `pvr_bridge_dispatch`（有 `file` 可查 context）；r406 模式 |
| 用户态测试程序 | 完整流程：INIT(2) → Connect → Create(0x12) → Test(0xFE) |

### DM 包布局（待活体验证）

| 偏移 | 字段 | 来源 |
|---|---|---|
| +0x0c | opcode 0x66 | [MEASURED] r365 |
| +0x28/+0x2c | TA buffer VA (u64) | [INFERRED] 3D 类比 r381 |
| +0x30 | TA buffer size (360) | [INFERRED] 3D 类比 r381 |
| +0x48 | wire_id | [MEASURED] r365 |
| +0x4c | pid | [MEASURED] r365 |

### 三种预期结果（未实测）

- **status=0**：固件完成（0x100）——布局正确，TA 被执行
- **status=1**：超时（submitted-but-ignored）——布局错误或固件需更多
- **status=2**：提交失败——路径问题

## 阻塞分析

**现象**：`0x82:0x12`（创建 render context）返回 `-ENODEV`；`dmesg`：`render context create failed: -19`。

**根因**：`mt_render_context_create` → `pvr_session_acquire` → `g->trial.pinned && g->trial.connected` 为 false。

**排除**：
- 原版桥（`git stash` 测试修改，重建）**同样失败** → 非本轮改动所致
- Probe 仍加载且绑定（`mt_guest_probe` ref=0，driver 绑定正常）
- 用户冷重启后 r407 观察器工作（但观察器不需要 trial）

**推断**：trial 自冷重启后未重建，或桥重载破坏了 trial 状态。需独立轮次诊断（probe trial 重建流程）。

## 安全

- Pre-live T1/T2/T3 全过（10 tests）；`(3,0x66)` 白名单确认
- 一次桥重载（`safe_rmmod.sh`，ref=0 确认）；`timeout` 未进临界区
- 无 oops/WARN/hang；未尝试重启（遵守用户指示）
- 测试修改未提交；生产代码零变更（门控默认关）

## 门禁

- `make -C mt-vgpu-guest check-offline`：待跑（本轮有桥修改，需确认）
- `make kernel` W=1：**零警告**（测试构建）
- 反向验证：未做（活体阻塞；实现层面门控已在 r411 验证）

## 交付物

- 本报告 `reports/r412-real-ta-blocked-trial.md`
- 测试钩子源码（未提交，`build/traces/r412/hook.c`）
- 用户态测试（`build/traces/r412/ta_real_test3.c`）

## 诚实边界

- **未做活体真实 TA 提交**：trial 阻塞，固件未收到任何真实 TA 包
- DM 布局 VA/size 偏移仍为 [INFERRED]，未验证
- 360B 内容语义（Q0/Q1/Q3/Q4）为最小构造，未验证固件接受度
- 实现已完成并编译，待 trial 恢复后可立即测试

## 下一步

1. **P0**：诊断 trial 未建立根因（probe trial 重建流程；可能需用户冷重启）
2. **P1**：trial 恢复后，重跑 r412 活体单发
3. **P2**：根据固件响应（完成/忽略/FAULT）调整 DM 布局或条目内容
