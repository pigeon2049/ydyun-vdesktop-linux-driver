# r368: wire 6 悬挂系生产完成路径缺失所致；r367 所述 UAF 经代码证伪——在载桥 kfree 为干净释放，"修复"实引入泄漏（只读诊断）

**结论**：
1. **wire 6 永久悬挂的根因是结构性的**：生产事件路径（probe `mt_runtime_event` → 通用 `mt_marker_complete`）拒绝 TA 完成码 `0x100`；而能处理 `0x100` 的 `mt_marker_complete_ta` 在生产代码中**从未被调用**（仅 r366 测试模块调用）。且 marker 框架**无任何超时机制**，故 wire 6 永久悬挂，非暂时延迟。
2. **r367 所述 UAF 经代码+git 双重证伪**：TA op（`mt_marker_submit_ta_work`，自 r366 起未变，r367 未动该头文件）**从未**将 `work->context` 写入 `m->context`（`m->context = c` 只存在于 TQX/context 两个无关 op）；`m` 为 kzalloc，`m->context` 恒为 NULL；`mt_marker_complete_ta` 的 `if (m->context)` 恒为假，**不可能解引用已释放内存**。在载桥（`4a78b331`）成功路径的 `kfree(ctx)` 是**干净释放**，无 UAF 风险——即使 firmware 此刻完成 wire 6 也不触发。
3. **r367 的"修复"基于错误前提，实际引入泄漏**：去掉成功路径 `kfree` 后，`ctx` 无人接管（op 明确注释 "Marker-level: no context ownership"），每次成功 dispatch 泄漏一个 `struct mt_execution_context`（~64B）。所谓"`m->context` 在 completion 路径未释放"的泄漏不存在（恒为 NULL，无物可释）。
4. **恢复方案**：用已有 `mt_drain_pending.ko`（已构建，vermagic `6.12.111+deb13-amd64` 匹配）清 wire 6 → refcnt 归 0 → `rmmod` → **回退 kfree 删除**（恢复干净释放，消除泄漏）后按 r360 流程重载。另需在 r369+ 补齐**生产 TA 完成路径**（否则后续 TA marker 仍将永久悬挂）。

本轮为**只读诊断**：未执行任何 rmmod/insmod/提交/触发；仅 sysfs、dmesg、`/proc` 只读与源码分析。

## 只读诊断证据

- wire 6 提交于 dmesg `[26595.505338]`，当前 uptime `27535s`，已悬挂 **~940s（~15.7min）**，其后零完成事件（见 `r368-readonly-state.txt`）。
- `mt_pvr_bridge` refcnt=1（`lsmod` 与 `/sys/module/.../refcnt` 一致；`holders/` 为空，pin 系 marker 内部 `__module_get`，by design）。
- `mt_guest_probe` ref=1，未动。
- 在载桥 build-id `4a78b331fd57e1cf…`，与 r367 一致。
- `dmesg | grep -icE 'oops|BUG:'` → 0，无 oops、无 WARN/BUG。

## UAF 证伪链（实测代码，非推断）

1. **op 从未转移 context**：`grep -n 'm->context\s*=' kernel/mt_marker_fence.h` 仅命中 186（`mt_marker_submit_context`）、269（`mt_marker_submit_tqx_work`）两个无关 op；TA op（383 行起）内只有 `c = work->context`（局部读）与 `work->context = NULL`（消费调用方指针），**无 `m->context` 赋值**。`git diff a953af9 bba124b -- kernel/mt_marker_fence.h` 为空——r366 起未变。
2. **completion 守卫恒为假**：`m` 经 `kzalloc` 分配，`m->context` 恒 NULL；`mt_marker_complete_ta`（505–508 行）`if (m->context)` 恒不成立，解引用不可达。
3. **误判来源推断**：TQX op（269 行）确有 `m->context = c; /* active_jobs already belongs to this prepared owner. */`，r367 分析极可能将其模式误套用于 TA op。
4. **推论**：在载桥成功路径 `kfree(ctx)` 释放的是无引用的孤立分配——干净释放；即使 wire 6 此刻完成，也无 UAF 路径。**UAF 触发可能性：零。**

## wire 6 悬挂根因（结构性）

- 生产完成路径：`mt_runtime_event`（`kernel/mt_guest_probe.c:265`）→ 通用 `mt_marker_complete` → `mt_fw_event_matches`（`kernel/mt_fw_event.h:28`）要求 `words[1]==0`，对 TA 完成码 `0x100` 返回 `-EOPNOTSUPP`。
- `mt_marker_complete_ta`（接受 `0x100`）在全树唯一调用者是 `build/traces/r366-live/mt_live_ta_work.c:83`（r366 测试模块，gitignored）——**生产零调用**。
- **无超时机制**：全树无 `delayed_work`/`watchdog`；唯一的 5s `dma_fence_wait_timeout` 是 submit op 的**输入依赖**等待，非完成超时。
- **次生风险**：若 firmware 已发出 `0x100` 事件，`mt_fw_event_drain` 在 `-EOPNOTSUPP` 处 break 且不推进 tail——该事件将**毒化 DM3 事件队列**（后续事件全部被堵）。drain wire 6 后 `s->count[DM3]` 归 0，后续 `0x100` 事件改走 `t->events` 暂存，毒化解除。

## 恢复方案（分步；本轮不执行，待确认）

### 步骤 1：drain wire 6（`mt_drain_pending.ko`，已构建）
- **前置**：复核本轮只读状态无变化（wire 6 仍 pending、refcnt=1、无 oops）；确认 `.ko` vermagic 匹配（已验：`6.12.111+deb13-amd64`）。
- **动作**：`insmod mt_drain_pending.ko enable=1`（全新瞬时模块，不碰 frozen probe/bridge）→ 观察 dmesg `drained=1` → 确认 refcnt→0 → `rmmod mt_drain_pending`。
- **安全性**：对 wire 6 的 marker：`m->job.state` 为 `MT_JOB_EMPTY`（kzalloc，op 未 publish）→ 跳过 `mt_work_job_complete`；`m->context` 为 NULL → 跳过解引用；无 fence waiter（r367 harness 已结束）；`dma_fence_set_error(-ETIMEDOUT)`+signal+put → `mt_marker_release` → `module_put` → refcnt 归 0。只读 MMIO 诊断无副作用。
- **回滚**：若 drain 后 refcnt 未归 0 或出现 oops → 停止，不进入步骤 2，上报。

### 步骤 2：rmmod 桥
- **前置**：refcnt=0 已确认；先将当前在载桥 `.ko`（`4a78b331` 构建）保存为回滚件。
- **动作**：`rmmod mt_pvr_bridge`（应成功）；确认 probe ref 仍为 1、dmesg 干净。
- **回滚**：rmmod 失败 → 停止上报，不强行操作。

### 步骤 3：源码修正 + 重载（关键纠正）
- **修正内容**：回退 r367 的 kfree 删除——恢复成功路径 `kfree(ctx)`，并修正注释（删除"ownership transferred to m->context" 的错误表述，注明 marker-level 无 context 绑定）。理由：UAF 不存在（已证伪）；当前源码每成功 dispatch 泄漏 ~64B；与"op 明确无 ownership"的设计一致。
  - 备选（不推荐）：保留现状，接受有界泄漏。仅当未来 R2b 让 op 真正绑定 `m->context` 时，才需要"成功路径不释放 + completion 路径释放"的完整 Design B——那是另一个设计，不是现在。
- **动作**：按 r360 流程构建 → 上机 → dmesg 确认注册 → L3 全绿 → freeze 恢复（probe ref 1 / bridge ref 0）。
- **回滚**：新桥加载失败或 dmesg 异常 → rmmod，装回步骤 2 保存的 `4a78b331` 构建。

### 步骤 4（r369+，另立轮次）：补齐生产 TA 完成路径
- 在 `mt_runtime_event` 中对 TA marker（DM3 + 0x66 类）路由到 `mt_marker_complete_ta`，或扩展通用匹配器接受 `0x100`。否则任何未来的 TA marker 都将重演 wire 6 式永久悬挂。这是真正的 R2b 缺口。

## R5 澄清

任务所述"`m->context` 在 completion 路径未释放"的泄漏**不存在**（恒 NULL）。真实存在的泄漏是**当前源码**成功路径的 `ctx`（~64B/次），修复点在 **dispatch 成功路径恢复 `kfree`**，而非 completion 路径——因为 completion 根本拿不到它（设计如此）。

## 诚实边界

- 本轮未触碰任何硬件状态；所有"若 firmware 完成"的推演基于代码路径分析，未实测（实测将构成触发动作，本轮禁止）。
- UAF 证伪基于源码+git；未对在载二进制做反汇编（task 已明示在载行为，且 op 二进制与源码一致——头文件自 r366 未变）。
- wire 6 是否曾发出 `0x100` 事件未实证（需读 firmware 队列，属诊断性 MMIO，本轮未做）；两种情形下恢复方案相同。
- `mt_drain_pending.ko` 的安全性基于代码审查，未 live 加载验证（本轮禁止）。

## 文件

- 报告：本文件 `mt-vgpu-guest/reports/r368-wire6-uaf-reassessment.md`
- 证据：`mt-vgpu-guest/reports/r368-readonly-state.txt`、`mt-vgpu-guest/reports/r368-code-refs.txt`（0600）
- 源码分析：`kernel/mt_marker_fence.h`（op/completion）、`kernel/mt_fw_event.h`（matcher/drain）、`kernel/mt_guest_probe.c`（`mt_runtime_event`）、`kernel/recovery/mt_drain_pending.c`（恢复工具）、`kernel/recovery/mt_pvr_bridge.c`（dispatch）

## 门禁

- `make -C mt-vgpu-guest check-offline`：待跑（本轮纯文档+证据，预期全绿）
