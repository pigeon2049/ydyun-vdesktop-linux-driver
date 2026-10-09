# r457：TA 包缺失 VM 信息（+0x18/+0x20）——最可能根因，opcode 0x66 低嫌疑

**结论：我方 TA DM 包缺失 GPU VM 标识（+0x18 root_pa、+0x20 process_id），而参考包构造器 `mt_work_opcode()` 的配套函数 `mt_work_command_encode()` 明确填写这两处。工作路径（TQX/3D）经 `mt_execution_context_inputs()` 填写，TA 路径的 `mt_fw_ta_real_command()` 绕过它。缺失 VM 信息 → 固件无法翻译 TA VA → MMU fault → hang。opcode 0x66 被 proxy 识别（返回 TA 类完成码 0x100），低嫌疑。**

## 1. 0x50B 包 opcode（0x66）深度分析

### 1.1 我方调用链 [MEASURED]

```
mt-ta-readback (userspace)
  → ioctl 0x82:0xFD → pvr_cmd_ta_readback (mt_pvr_bridge.c:5275)
    → mt_ta_submit_real (mt_pvr_bridge.c:5814)
      → mt_bridge_submit_ta_work → mt_marker_submit_ta_work (mt_marker_fence.h:586)
        → mt_marker_submit_engine_work (ops = mt_ta_submit_ops, dm = MT_FW_DM_TA = 3)
          → ops->build_command = mt_ta_submit_build
            → mt_fw_ta_real_command (MT_TA_REAL_PACKET=1)
              → +0x0c = MT_FW_TA_OPCODE = 0x66
          → mt_fw_queue_try_submit(s->queue, dm=3, ...)
          → writel(3, registers+0xb00)   // DM kick，被 host 截获
```

`MT_FW_TA_OPCODE` 定义（`kernel/mt_ta_submit.h:49-52`）：
```c
/* D4 [INFERRED -> TO-VALIDATE]: opcode for the 0x50-byte firmware command
 * (+0x0c). 0x66 = RGXVertex/UniversalQueue in mt_work_opcode()
 * (kernel/mt_work_command.h); TA consumes vertex/tile work. If the
 * firmware NAKs it, capture the Windows KMD's TA opcode as follow-up. */
#define MT_FW_TA_OPCODE 0x66U
```

`mt_work_opcode()`（`kernel/mt_work_command.h:14-24`）：
```c
static inline u32 mt_work_opcode(u32 type)
{
    switch (type) {
    case 1: return 0x67; /* Transfer Op */
    case 3: case 11: return 0x66; /* RGXVertex / UniversalQueue */
    case 5: return 0x68; /* RGXCompute */
    case 6: return 0x65; /* Preempt: not enabled by prepare below */
    case 9: return 0x69; /* CopyEngine */
    default: return 0x64;
    }
}
```
注释称 "Descriptor type numbers from 1400136dc"——疑似仿真器（SIMSubmitCommandVirtual）参考，非真实固件文档。

### 1.2 真实 KCCB KICK [MEASURED]

`src/official-vgpu-2.3.0/.../rgx_fwif_km.h:1334`：
```c
RGXFWIF_KCCB_CMD_KICK = 101U | RGX_CMD_MAGIC_DWORD_SHIFTED,  // = 0x2ABC0065
RGXFWIF_KCCB_CMD_MMUCACHE = 102U | RGX_CMD_MAGIC_DWORD_SHIFTED, // = 0x2ABC0066
```
`RGXFWIF_KCCB_CMD_KICK_DATA`（:1019-1027）首字段为 `PRGXFWIF_FWCOMMONCONTEXT psContext`（固件上下文指针）。

**关键澄清**：KCCB 与 DM 队列是两个不同接口。
- KCCB：KMD → 固件的命令队列。KICK（0x2ABC0065）告诉固件"kick 某个 DM"。
- DM 队列：各数据主机（TA/3D/TQX）的命令队列。我方 `writel(dm, registers+0xb00)` 写的是 DM 队列。

我方 0x66 与 KCCB 的 0x2ABC0065 不在同一命名空间，直接对比是范畴错误。r453 的"0x66=MMUCACHE 命令号"类比仅在 KCCB 命名空间成立，不适用于 DM 队列。

### 1.3 proxy 识别 0x66 [MEASURED]

- r414（全零 header）：固件返回 **0x100**（`MT_FW_TA_COMPLETE_CODE`），注释称 "0x66-class firmware completion code"（`mt_ta_submit.h:55-57`）。
- proxy 对 0x66 包返回 TA 类完成码 → **proxy 将 0x66 识别为 TA 相关**。
- 若 opcode 完全错误，应得立即错误而非 5s 超时。

**结论：opcode 0x66 低嫌疑** [INFERRED]。D4 的 TO-VALIDATE 可保留，但不应作为主攻方向。

## 2. 缺失的 VM 信息——核心发现 [MEASURED]

### 2.1 参考包构造器填写 +0x18/+0x20

`mt_work_command_encode()`（`kernel/mt_work_command.h:28-46`）：
```c
mt_fw_put32(packet, 0x0c, ...opcode...);
mt_fw_put32(packet, 0x48, fence);
mt_fw_put32(packet, 0x08, (input->submit_flags >> 5) & 4);
mt_fw_put64(packet, 0x18, input->root_pa);      // ← GPU 页表根物理地址
mt_fw_put64(packet, 0x20, input->process_id);   // ← 进程 token
mt_fw_put64(packet, 0x28, input->command_va);
mt_fw_put32(packet, 0x30, input->bytes);
mt_fw_put32(packet, 0x4c, input->process_pid);
```

`mt_execution_context_inputs()`（`kernel/mt_execution_context.h:112-127`）：
```c
*out = (struct mt_work_command_inputs){
    .root_pa = c->process->vm->tables->backing.gpu_pa,
    .process_id = c->process->token, .process_pid = c->process->pid,
    .command_va = r->command_va, ...};
```
工作路径（TQX/3D via `mt_marker_submit_context`）经此填写 root_pa/process_id。

### 2.2 TA 路径绕过它 [MEASURED]

`mt_fw_ta_real_command()`（`kernel/mt_marker_fence.h:334-347`）：
```c
static inline void mt_fw_ta_real_command(void *command, u32 fence, u32 pid,
                     u64 ta_va, u32 ta_size)
{
    memset(command, 0, MT_FW_COMMAND_BYTES);
    mt_fw_put32(command, 0x0c, MT_FW_TA_OPCODE);   // 0x66
    mt_fw_put32(command, 0x48, fence);              // wire_id
    mt_fw_put32(command, 0x4c, pid);
    mt_fw_put32(command, MT_TA_DM_PKT_TA_VA_LO, ...); // +0x28
    mt_fw_put32(command, MT_TA_DM_PKT_TA_VA_HI, ...); // +0x2c
    mt_fw_put32(command, MT_TA_DM_PKT_TA_SIZE, ta_size); // +0x30
    // +0x18 (root_pa) = 0, +0x20 (process_id) = 0  ← 缺失！
}
```

`mt_ta_submit_real` 设置了 `work.context = &rctx->exec_ctx_ta`（`mt_pvr_bridge.c:5874`），
但 `mt_marker_submit_engine_work` 只用 context 做 `c->route.dm` 校验和 fence 归属，
**包构造钩子 `build_command(packet, wire_id, pid, params)` 签名中根本没有 context 参数**，
`mt_ta_submit_build` → `mt_fw_ta_real_command` 拿不到 `root_pa`/`process_id`。

### 2.3 为什么导致 hang [INFERRED 高置信]

- TA buffer VA（staging BO）和 RgnHeader VA（0x7c000000）是 **guest GPU 虚地址**。
- 无 `root_pa` → 固件/host 不知道用哪套页表翻译 → GPU MMU fault。
- MMU fault 未解决 → TA DM 等待 → **5s 超时**（非立即错误）。
- 与观测完全吻合：
  - r414 全零 header → 固件走"无工作"快路径，**不解引用 TA VA** → 219µs 完成。
  - r418+ 非零 header → 固件尝试执行，解引用 TA VA → fault → hang。

## 3. Bridge 参数缺失（架构层面）[MEASURED]

真实 `PVRSRVRGXKickTA3DKM`（`rgxta3d.h:438`）约 40 个参数，含：
- `psKMHWRTDataSet`（RT dataset）、`psZSBuffer`、`psMSAAScratchBuffer`
- Client TA/3D fence & update sync prims、PR fence、check fence
- `ui32NumberOfDrawCalls`、`ui32NumberOfIndices`、`ui32NumberOfMRTs`
- `pui8TADMCmd` / `ui32TACmdSize`（我方仅提供此二元组）

我方 `struct mt_ta_submit_params` 有对应槽位（`mt_ta_submit.h:66-97`），
但 `mt_ta_submit_real` 只填 `ta_cmd_va`/`ta_cmd_size`，其余为 0，
且 `mt_ta_submit_validate` **主动拒绝**非零的 `ta_upd_count`/`ta_fence_count`（R5 门禁）。

然而这是**架构性绕行**（直接写 DM 队列，绕过 KMD 的 `PVRSRVRGXKickTA3DKM`），
不是包字段缺失。host proxy 若做完整转译（读 guest 内存取 TA buffer 内容，
自行构造真实 KCCB），则我方缺失的参数由 host 补足。无法从 guest 侧验证，
列为中嫌疑。

## 4. 根因排序

| 候选 | 证据 | 等级 | 排序 |
|---|---|---|---|
| **TA 包缺失 VM 信息（+0x18/+0x20）** | 参考构造器明确填写；TA 路径绕过；MMU fault 解释 hang；零/非零行为差异吻合 | [INFERRED 高] | **P0** |
| Bridge 参数缺失（psContext/RT dataset/fence） | 真实 KickTA3D 约 40 参数，我方仅 2 个；但属架构绕行，host 可能补足 | [INFERRED 中] | P1 |
| opcode 0x66 错误 | proxy 返回 TA 类完成码 0x100，识别为 TA；错 opcode 应立即错误 | [INFERRED 低] | P2（保留 TO-VALIDATE） |

## 5. r458 修复方案

**目标**：TA 包填写 +0x18（root_pa）与 +0x20（process token），对齐参考包格式。

改动点（均在现有文件内，不新增架构）：

1. `kernel/mt_ta_submit.h` — `struct mt_ta_submit_params` 新增：
   ```c
   u64 vm_root_pa;   /* GPU page-table root PA for TA VA translation (+0x18) */
   u64 vm_token;     /* process token identifying the GPU VM (+0x20) */
   ```
2. `kernel/recovery/mt_pvr_bridge.c` — `mt_ta_submit_real` 在 `work.params` 赋值处新增：
   ```c
   work.params.vm_root_pa = rctx->exec_ctx_ta.process->vm->tables->backing.gpu_pa;
   work.params.vm_token   = rctx->exec_ctx_ta.process->token;
   ```
   （`exec_ctx_ta` 已存在且已校验；`mt_execution_context_inputs` 用完全相同的表达式。）
3. `kernel/mt_marker_fence.h` — `mt_fw_ta_real_command` 新增 `u64 root_pa, u64 token` 参数，
   写 `+0x18`/`+0x20`；`mt_ta_submit_build` 透传 `params->vm_root_pa`/`params->vm_token`。
   - 0x82:0xC 路径（`mt_ta_params_from_musakickgfx2`）保持 0（observer-only，不执行）。
4. 测试：
   - C：包布局测试断言 +0x18/+0x20 非零（当 params 提供时）。
   - Python：`mt_ta_submit.h` 结构体字段存在性 + `mt_pvr_bridge.c` 赋值点存在性。
   - 反向验证：回退任一改动点 → 测试精确 FAIL。
5. 门禁：`check-offline` 全绿；`kernel` W=1 零警告。

**预期**：若 MMU fault 是 hang 根因，r459 活体（需用户冷重启）应观察到行为变化
（完成或新的错误码，而非 5s 超时）。

## 6. 诚实边界

- [MEASURED]：包字段对比（参考 vs TA 路径）；`build_command` 签名无 context；KCCB/DM 接口区分；proxy 返回 0x100。
- [INFERRED 高]：缺失 VM 信息 → MMU fault → hang。
- [UNKNOWN]：host proxy 实际如何处理 DM3 包；是否用 pid 推导 VM；固件 hang 精确位置。
- 本轮纯离线，零硬件触碰，无生产代码变更。

## 7. 交付物

- 报告：`mt-vgpu-guest/reports/r457-ta-packet-missing-vm-info.md`
- 门禁：`make -C mt-vgpu-guest check-offline` 全绿；`make kernel` W=1 零警告（无代码变更，复核通过）
- 本地提交（不 push）
