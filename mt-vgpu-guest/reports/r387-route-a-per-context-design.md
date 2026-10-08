# r387：R6 Route A 设计——per-context 状态对象模型（离线设计，零硬件触碰）

## 结论

**Route A（用户已决策）**：`MT_PVR_KIND_CONTEXT` 从空 token 扩展为携带真实状态的对象——11 BO + firmware 执行上下文 + per-context VM。R5 的 per-file VM 作为过渡保留，渐进式迁移（先共存，后切换）。

**核心设计**：
1. `struct mt_pvr_render_context` 挂载 11 BO（`mt_gfx_context.h` 规格）、CSW、execution process/context、per-context VM
2. `0x82:0x12` create 时分配全部资源；destroy 时逆序释放；kick 侧由 handle 解析到状态
3. R6-1~R6-6 分解为 6 个工作项，R6-1~R6-4 可立即启动，R6-5/R6-6 需先调研

## 1. 对象模型设计

### 1.1 现状（实测）

`kernel/recovery/mt_pvr_bridge.c:283`：
```c
struct mt_pvr_object {
    struct list_head link;
    u64 handle;
    u32 kind;
    u64 arg0;  /* TDM 存 2 个字段，其余为 0 */
    u64 arg1;
};
```

`pvr_cmd_render2_create`（:3287）仅 `pvr_object_new(file, MT_PVR_KIND_CONTEXT)` + 返回 handle，IN 的 `priv_data`/`priority` 被丢弃。

### 1.2 Route A 扩展设计

```c
/* Per-context 真实状态（Route A） */
struct mt_pvr_render_context {
    /* 11 BO：mt_gfx_context_bo_specs，86,300B（实测 mt_live_3d.c） */
    struct mt_bo bos[MT_GFX_CONTEXT_BO_COUNT];
    u64 vas[MT_GFX_CONTEXT_BO_COUNT];
    bool bos_ready[MT_GFX_CONTEXT_BO_COUNT];  /* 部分失败时清理用 */

    /* Firmware 执行状态（实测 mt_live_3d.c:296） */
    struct mt_execution_process process;
    struct mt_execution_context exec_ctx;
    bool exec_ready;

    /* CSW：248B（mt_gfx_context_build_csw） */
    u8 csw[MT_GFX_CONTEXT_CSW_BYTES];

    /* Per-context VM（R5 演进，见 §3） */
    struct mt_bridge_ta_vm *vm;
    u64 vm_base_va;  /* per-context VA 基址，避免多 context 碰撞 */

    /* 状态标志 */
    bool resources_ready;  /* 全部就绪后置 true */
};

struct mt_pvr_object {
    struct list_head link;
    u64 handle;
    u32 kind;
    u64 arg0;
    u64 arg1;
    /* Route A 新增：context 挂载真实状态 */
    struct mt_pvr_render_context *render_ctx;  /* kind==CONTEXT 时有效 */
};
```

**设计决策**：
- 用指针而非内嵌：`struct mt_pvr_object` 被所有 kind 共用，内嵌 11 BO 会浪费内存（SYNC/PMR 等不需要）。指针按需分配。
- `vm_base_va`：per-context VA 分区。R5 的 `MT_TA_CMD_VA_BASE=0x70000000` 是 per-file 的；per-context 时，context N 使用 `0x70000000 + N * MT_CONTEXT_VA_STRIDE`（STRIDE 待定，建议 16MB，待验证）。

### 1.3 与 mt_live_3d.c 的对应关系（实测）

| mt_live_3d.c（r37–r41 验证） | Route A 设计 | 说明 |
|---|---|---|
| `st->context_bos[11]` | `render_ctx->bos[11]` | 直接复用 |
| `st->context_vas[11]` | `render_ctx->vas[11]` | 直接复用 |
| `st->process` / `st->context` | `render_ctx->process` / `exec_ctx` | 直接复用 |
| `st->csw[248]` | `render_ctx->csw[248]` | 直接复用 |
| `st->space`（VM） | `render_ctx->vm` | R5 的 `mt_bridge_ta_vm` 演进，见 §3 |
| `st->command_bo` | 不挂载 | Command BO 是 per-kick transient，R5 VM 负责映射 |

## 2. 生命周期设计

### 2.1 Create（0x82:0x12 真实化，R6-2）

```
pvr_cmd_render2_create(file, cmd):
  1. pvr_in() 解析 12B IN {priv_data, priority}  // 当前丢弃，保留字段以备后用
  2. obj = pvr_object_new(file, MT_PVR_KIND_CONTEXT)
  3. obj->render_ctx = kzalloc(sizeof(*render_ctx))
  4. for i in 0..10:
       - mt_bo_create(&bos[i], d->buffers.ops, &d->buffers, PAGE_ALIGN(spec.bytes))
       - mt_live_3d_write_bo() 写入 mt_gfx_context_data.h 初始数据
       - vm = render_ctx->vm; bind(bos[i], vas[i] = vm_base + i*STRIDE)
       - 失败时回滚已分配的 BO（bos_ready 标记）
  5. mt_gfx_context_build_csw(csw, vas)  // 248B
  6. mt_execution_process_create(&process, &space->vm, tgid)
  7. mt_execution_context_create(&exec_ctx, &process, 5, 0)  // node_type=5 → DM2
  8. render_ctx->resources_ready = true
  9. pvr_out() 返回 handle
```

**失败回滚**：任一步失败，逆序释放已分配资源，`kfree(render_ctx)`，`pvr_object` 从链表删除并释放，返回错误码。不留半初始化状态。

**门禁**：check-offline + kernel W=1 零警告 + 活体 V1（单次 create 成功，dmesg 确认 11 BO 绑定，无 oops）。

### 2.2 Destroy（0x82:0x13 真实化，R6-3）

```
pvr_cmd_handle_release(file, cmd, MT_PVR_KIND_CONTEXT):
  1. obj = pvr_object_find(file, handle, MT_PVR_KIND_CONTEXT)
  2. if (!obj) return -ENOENT
  3. ctx = obj->render_ctx
  4. if (ctx && ctx->resources_ready):
       - mt_execution_context_destroy(&ctx->exec_ctx)
       - mt_execution_process_destroy(&ctx->process)
       - for i in 0..10: if (bos_ready[i]) { unbind(vas[i]); mt_bo_put(&bos[i]); }
       - mt_bridge_ta_vm_destroy(ctx->vm)
  5. kfree(ctx)
  6. list_del(&obj->link); kfree(obj)
```

**当前行为**：仅 `list_del` + `kfree(obj)`（空 token）。R6-3 在其基础上增加步骤 3–5。

**门禁**：check-offline + kernel W=1 + 活体 V2（create→destroy，确认 BO ref 归零、VM 销毁、dmesg 无泄漏警告）。

### 2.3 Kick 侧解析（R6-4）

**0x82:0xC（TA）当前**：`mt_pvr_bridge.c:4018` 仅 dmesg 打印 `in.h_render_context`，不使用。

**R6-4 设计**：
```
pvr_cmd_musakickgfx2(file, cmd):
  1. pvr_in() 解析 268B IN（含 h_render_context@偏移待确认）
  2. obj = pvr_object_find(file, in.h_render_context, MT_PVR_KIND_CONTEXT)
  3. if (obj && obj->render_ctx && obj->render_ctx->resources_ready):
       ctx = obj->render_ctx
       // 使用 per-context 状态：
       // - ctx->exec_ctx 供 firmware 提交
       // - ctx->vm 供 command buffer 映射（替代 file->ta_vm_ctx）
     else:
       // Phase 1 回退：使用 per-file ta_vm_ctx（当前行为，保持 marker 测试通过）
       // Phase 2（验证后）：返回 -EINVAL，要求有效 context
  4. ... 现有提交流程 ...
```

**0x82:0x14（3D）**：同理，`pvr_cmd_kickta3d5_observe` 目前仅校验 handle 存在性；R6-4 后解析到真实状态供 `submit_3d_work` 使用。

**门禁**：check-offline + kernel W=1 + 活体 V3（带真实 context handle 的 TA marker，确认走 per-context 路径，OUT 回填正常）。

## 3. R5 迁移策略：渐进式共存

### 3.1 为什么不一次性切换

- r372/r373/r377 的 marker 测试依赖 per-file `ta_vm_ctx` 且全通。一次性切换若引入 bug，会同时破坏已验证路径。
- 真实 UMD 的 context 行为尚未活体验证（r384 标注[待验证]），per-context VM 的 VA 分区方案需实测调优。

### 3.2 两阶段计划

**Phase 1（R6-1~R6-4）：共存**
- `struct mt_pvr_file` 保留 `ta_vm_ctx`（per-file）
- `struct mt_pvr_object` 新增 `render_ctx`（per-context）
- Kick 路径：优先用 per-context（若 handle 有效且 ready），否则回退 per-file
- 门禁：所有现有 marker 测试必须通过（回归）

**Phase 2（验证后）：切换**
- 前提：per-context 路径经活体验证（V1–V3 全绿 + 真实 UMD 调用成功）
- 删除 `file->ta_vm_ctx` 及回退逻辑
- Kick 路径：无有效 context 直接 `-EINVAL`
- 单独一轮（r38x），含完整回归

### 3.3 VA 分区设计（待验证）

| 方案 | 描述 | 优点 | 缺点 |
|---|---|---|---|
| A. Per-context 独立 VM | 每个 context 一个 `mt_bridge_ta_vm`，VA 基址 `0x70000000 + idx*16MB` | 隔离彻底 | VM 数量多，内存开销 |
| B. 共享 VM + 分区 | 单个 per-file VM，per-context 分配 VA 子范围 | 开销小 | 需分配器，分区管理复杂 |

**本轮推荐**：方案 A（简单直接，16MB stride 足够容纳 11 BO 的 86KB + command buffer）。若实测发现 VM 过多导致性能问题，再评估方案 B。标注为[待验证]。

## 4. R6-1~R6-6 工作项分解

| # | 工作项 | 内容 | 前置 | 门禁 | 活体 |
|---|---|---|---|---|---|
| R6-1 | 对象模型扩展 | 定义 `struct mt_pvr_render_context`；`mt_pvr_object` 加 `render_ctx` 指针；`pvr_object_new` 初始化为 NULL | 无 | check-offline + kernel W=1 | 无（纯结构） |
| R6-2 | Create 真实化 | `pvr_cmd_render2_create` 实现 §2.1 流程；复用 `mt_live_3d.c` 的 BO/CSW/exec 逻辑（抽取为 helper） | R6-1 | check-offline + kernel W=1 + 反向验证 | V1：单次 create 成功，11 BO 绑定，无 oops |
| R6-3 | Destroy 真实化 | `pvr_cmd_handle_release` 增加 §2.2 的资源释放 | R6-2 | 同上 | V2：create→destroy 无泄漏 |
| R6-4 | Kick 侧解析 | `0x82:0xC`/`0x82:0x14` 按 §2.3 解析 handle；Phase 1 保留回退 | R6-2 | 同上 + 回归（marker 测试全过） | V3：带 context 的 marker 走 per-context 路径 |
| R6-5 | CCB（0x88:0x5） | Server 侧 CCB 分配与管理 | **先调研**（r56–r57 的 CCB 窗口定义需落实） | 待定 | 待定 |
| R6-6 | TDM（0x89:0x8） | Transfer context 的 BO 需求 | **先调研**（可能比 3D 简单） | 待定 | 待定 |

**依赖图**：
```
R6-1 → R6-2 → R6-3
         ↓
       R6-4 → (Phase 2 切换)
R6-5（独立，需先调研）
R6-6（独立，需先调研）
```

**R6-5/R6-6 说明**：r384 已标注为[待验证]。建议各用一轮离线调研（仿 r384/r385 模式），再决定是否实现。

## 5. 设计决策与待验证点

### 已决策（本轮）
1. **Route A**：用户决策，per-context 状态，不试 Route B
2. **指针挂载**：`render_ctx` 为指针而非内嵌，避免非 CONTEXT kind 浪费内存
3. **渐进迁移**：Phase 1 共存 + 回退，Phase 2 切换（保护已验证的 marker 路径）
4. **复用 mt_live_3d.c**：11 BO 规格、init 数据、CSW 构建、exec 创建流程已验证，直接复用
5. **VA 分区方案 A**：per-context 独立 VM，16MB stride（简单优先）

### 待验证（后续轮次）
1. VA 分区 stride 的实际值（16MB 是否合适，需看 firmware 的 VA 限制）
2. TA-only context 是否需要全部 11 BO（或仅 TA state BO#4 即可）
3. `priv_data`/`priority`（0x82:0x12 IN）的语义（当前丢弃，真实 UMD 可能依赖）
4. CCB 的 server 侧定义（R6-5 前置调研）
5. TDM context 的 BO 需求（R6-6 前置调研）
6. 多 context 并发时的 firmware 行为（CSW 切换是否正确）

## 6. 标注

- **[实测]**：`mt_pvr_object` 结构（源码 :283）；`pvr_cmd_render2_create` 的 token 行为（源码 :3287）；`pvr_cmd_handle_release` 的 kfree 行为（源码 :3311）；`mt_live_3d.c` 的 11 BO + exec 流程（源码，r37–r41 活体验收）；R5 per-file `ta_vm_ctx` 挂载点（源码 :352）
- **[推断]**：VA 分区方案 A 的 16MB stride（基于 86KB BO + 余量的工程估计）；渐进迁移的两阶段计划（基于保护已验证路径的工程判断）；TA-only 可能不需要全 11 BO（基于 TA state BO#4 是 TA 专用的观察）
- **[待验证]**：§5 列出的 6 项

## 门禁

- 本轮纯设计、无代码改动：`make -C mt-vgpu-guest check-offline` 全绿（见下）。
- 引用路径核对：`kernel/recovery/mt_pvr_bridge.c`（283/3287/3311/352）、`kernel/mt_gfx_context.h`（11 BO 规格）、`kernel/recovery/mt_live_3d.c`（40–120/190–300）均存在。

## 证据

- `mt-vgpu-guest/reports/r387-evidence.txt`：关键源码摘录（对象模型、create/destroy handler、mt_live_3d.c 状态结构）。
