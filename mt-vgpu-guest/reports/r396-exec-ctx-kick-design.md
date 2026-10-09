# r396: exec_ctx 接入 kick 路径设计——render_ctx 双执行上下文（TA/3D）（离线）

## 结论

`render_ctx` 应持有**两个** `mt_execution_context`：既有 `exec_ctx_3d`
（node_type=5 → DM2，r389 已建）之外，新增 `exec_ctx_ta`（node_type=2 →
DM3）。两者共享同一 `process`（同一 VM、同一 11 BO）。TA kick
（`0x82:0xC`）传真实 `exec_ctx_ta` 替代一次性 throwaway ctx；3D kick
（`0x82:0x14`，门控关闭）传 `exec_ctx_3d`。分三阶段落地，本轮纯设计，
零行为变更。

## 1. 现状分析（实测）

### 1.1 `work->context` 今天是什么

`pvr_cmd_musakickgfx2`（`kernel/recovery/mt_pvr_bridge.c:4446`）每次 kick：

```c
ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
ctx->route.dm = MT_FW_DM_TA;   /* 只填这一个字段 */
work.context = ctx;
mt_bridge_submit_ta_work(s, &work, &fence);
...
kfree(ctx);                     /* 成功/失败都释放 */
```

这是一个**一次性 dm 标签**：除 `route.dm` 外全零，无 process、无 VM、
无 route（未经过 `mt_node_route_build`）。它的唯一作用是满足
`mt_marker_submit_ta_work` 的门禁检查（`mt_marker_fence.h:420`）：

```c
c = work->context;
if (!c || c->route.dm != dm)   /* dm = MT_FW_DM_TA */
    return -EOPNOTSUPP;
```

提交走的仍是硬编码 DM3 + `mt_fw_ta_marker_command`（0x66/wire_id/pid 三字
节）；op 明确不拿 context 所有权（"Marker-level: no context ownership"，
`work->context=NULL`，`m->context` 永不赋值——r368 已证伪 UAF）。

### 1.2 `render_ctx->exec_ctx` 今天是什么

`mt_render_context_create()`（`:4308`）经正式路径创建：

```c
mt_execution_context_create(&ctx->exec_ctx, &ctx->process, 5, 0);
```

`mt_node_route_build(5,·)` → `dm=2`（3D），`flags=1`，`capabilities=0xf`
（`mt_work_command.h:103`，r381 实证）。它是**真实**执行上下文
（process/VM/route 齐全），但 r391 只用了 `rctx->vm` 做映射隔离，
**未用于提交**——因为 TA 门禁要求 `route.dm==3`，而它是 dm=2。

### 1.3 node_type ↔ DM 对照（`mt_node_route_build` 实测）

| node_type | dm | 用途 |
|---|---|---|
| 1 | 1 | TQX（`submit_tqx_work` 要求 `route.type==1 && dm==1`） |
| 2 | 3 | **TA**（`flags=1, capabilities=8`） |
| 5 | 2 | 3D RGXCompute（r389/r381） |
| 8 | 2 | 3D（group=1 变体） |

`mt_device_profile_node()` 仅在 `!ce_version` 时拒绝 type 3/4；
**type=2 无条件通过**——创建 TA exec_ctx 无 profile 障碍。

### 1.4 真实提交路径已存在

`mt_marker_submit_context`（op #3，`mt_marker_fence.h:186`）是通用真实路径：

```c
mt_execution_context_inputs(&in, c, request); /* root_pa←process VM, token, command_va… */
mt_marker_submit_work(s, c->route.dm, c->process->vm, bo, &in, out);
m->context = c; c->active_jobs++;             /* fence 持有 context */
```

完成侧两条路径（`mt_marker_complete` / `mt_marker_complete_ta`）都已处理
`m->context`（`active_jobs--`）。TA 的 0x100 特殊匹配器
（`mt_fw_event_matches_ta`）与 context 所有权正交——**完成路径无需改动**。

## 2. 设计决策

### D1：双 exec_ctx，而非复用或改 route（已定）

- **A（采用）**：`exec_ctx_ta`（node_type=2→DM3）+ `exec_ctx_3d`
  （node_type=5→DM2，既有改名），共享同一 `process`。
  真实 RGX render context 本就同时驱动 TA 与 3D 两引擎；firmware 按
  DM/queue 区分，不按 context 区分。
- B（否决）：单 exec_ctx，每次 kick 改 `route.dm`——与 `active_jobs`
  记账竞态，且 `route` 语义上是创建时确定的。
- C（否决）：throwaway ctx 永久化——那 R6 的真实状态就白做了，
  真实 TA payload（`ta_cmd_va`→`command_va`）无处挂靠。

代价：每 context +72B（一只 `struct mt_execution_context`），create/
destroy 各多一次调用；`process.contexts` 引用计数天然容纳（`p->contexts++`
两次，`mt_execution_context_destroy` 的 `-EBUSY` 守卫即 bug 捕获器）。

### D2：TA 提交仍走 `submit_ta_work`，不改道通用 `submit_context`（已定）

`submit_ta_work` 承载三件 TA 专属事务，通用路径没有：
1. 0x100 完成码匹配（`mt_marker_complete_ta`，r365/r370）；
2. `mt_ta_sync_update_apply`（sync-prim 回写，`ta_upd_count>0` 时）；
3. D5/D8 诚实拒收（`kick_pr`、`ta_upd/fence_count` 的 `-EOPNOTSUPP`）。

真实 payload 阶段（Phase 3）只在该 op 内部把 marker 包换成
`mt_execution_context_inputs()` 构建的完整命令（root_pa/command_va/
bytes/type=3→0x66），DM、完成、回写逻辑不动。**完成分发无需改**：
bridge 的 `pvr_ta_wait_complete` 本就显式调 `mt_marker_complete_ta`。

### D3：三阶段落地，行为中性优先（已定）

- **Phase 1（r397）**：加 `exec_ctx_ta` 字段 + create（node_type=2）+
  destroy；kick 仍用 throwaway。零行为变更，验证"多一个真实 ctx
  不破坏任何东西"。
- **Phase 2（r397 或 r398）**：有-context kick 传 `&rctx->exec_ctx_ta`
  替代 throwaway；无-context kick 保留 throwaway（r391 Phase 1 回退不动）。
  marker 包、完成路径、回填**完全不变**——纯对象替换，r395 式回归
  （OUT.update_fence↔wire 精确匹配）即验证。
- **Phase 3（远期）**：`MT_TA_VM_READY` 开 + 真实 UMD payload 到达后，
  `submit_ta_work` 内用 `exec_ctx_ta` 构建完整命令并拿所有权
  （`m->context=c`）。前置：R5 真实映射、sync-prim 可解析。

## 3. 详细设计

### 3.1 结构变更（`kernel/mt_render_context.h`）

```c
struct mt_pvr_render_context {
    ...
    struct mt_execution_process process;
    struct mt_execution_context exec_ctx_3d;  /* node_type=5 → DM2（既有 exec_ctx 改名） */
    struct mt_execution_context exec_ctx_ta;  /* node_type=2 → DM3（新增） */
    ...
};
```

改名而非保留 `exec_ctx`：消除歧义，grep 可审计。+72B → 约 1616B。

### 3.2 Create（`mt_render_context_create`，8 步中插一步）

现行第 6–7 步之后加：

```
6. mt_execution_process_create(&ctx->process, vm, pid)
7a. mt_execution_context_create(&ctx->exec_ctx_3d, &ctx->process, 5, 0)
7b. mt_execution_context_create(&ctx->exec_ctx_ta,  &ctx->process, 2, 0)  // 新增
8. resources_ready = true（仅 7a+7b 全成功）
```

任一步失败逆序回滚（7b 失败 → 销毁 7a → 销毁 6 …），沿用 r389/r390
的单路经 `mt_render_context_destroy`。

### 3.3 Destroy（`mt_render_context_destroy`）

```c
mt_execution_context_destroy(&ctx->exec_ctx_ta);  /* 先 TA（顺序无关，都只动 process.contexts） */
mt_execution_context_destroy(&ctx->exec_ctx_3d);
mt_execution_process_destroy(&ctx->process);      /* contexts!=0 则 -EBUSY，WARN_ON 捕获 */
```

`active_jobs != 0` 时 `mt_execution_context_destroy` 返 `-EBUSY`
（`mt_execution_context.h` 既有守卫）——in-flight fence 未退休就 destroy
会被诚实拦截，不静默泄漏（对应 r374 V4）。

### 3.4 Kick 路径（`pvr_cmd_musakickgfx2`，Phase 2）

```c
if (rctx && rctx->resources_ready) {
    work.context = &rctx->exec_ctx_ta;   /* 真实 ctx，route.dm==3，门禁通过 */
    /* 无 kfree：owner 是 render_ctx（pvr_object 引用计数） */
} else {
    ctx = kzalloc(...); ctx->route.dm = MT_FW_DM_TA;  /* throwaway 回退（r391 不动） */
    work.context = ctx;
}
...
if (allocated_throwaway) kfree(ctx);      /* 仅回退分支释放 */
```

门禁 `c->route.dm != MT_FW_DM_TA → -EOPNOTSUPP` 对真实 ctx 同样通过；
r394 T2 白名单（TA 钉死 DM3）天然一致，**无需改白名单**（新增断言见 §5）。

### 3.5 3D 路径（`0x82:0x14`，门控关闭）

门控开启时 `mt_marker_submit_3d_work` 的门禁
（`c->route.dm != MT_FW_DM_3D → -EOPNOTSUPP`）直接接受
`&rctx->exec_ctx_3d`。与 TA 对称，无额外设计。

### 3.6 生命周期/所有权

| 阶段 | `m->context` | owner |
|---|---|---|
| Marker（Phase 1/2） | 永不赋值（r366/r368） | render_ctx（pvr_object），kick 借用不持有 |
| 真实 payload（Phase 3） | `m->context = exec_ctx_ta`，`active_jobs++` | fence；destroy 遇 `-EBUSY` 诚实失败 |

Marker 阶段无所有权转移，故 Phase 2 替换是行为中性的——这是本设计的
核心安全论据。

## 4. 与现有路径的关系

- **无-context kick**：throwaway 路径原样保留（r391 回退、r395 回归基线）。
- **Marker 回归**：包内容（0x66/wire_id/pid）、完成（0x100）、回填
  （OUT.update_fence）三者都不依赖 context 对象是真是假——r395 式验证
  可直接复用。
- **R5 per-file VM**：映射隔离仍按 r391（`kick_vm` 选择逻辑不动）；
  exec_ctx 只管"以谁的名义提交"，不管"页表怎么走"——两正交。
- **TQX 路径**：`translator.tqx_context` 独立，不受影响。

## 5. 工作项分解

| # | 内容 | 验证点 | 硬件 |
|---|---|---|---|
| r397-1 | 结构：`exec_ctx`→`exec_ctx_3d` 改名 + 新增 `exec_ctx_ta`；T2 加断言 `exec_ctx_ta.route.dm==3 && exec_ctx_3d.route.dm==2` | check-offline 全绿，kernel W=1 零警告 | 无 |
| r397-2 | create 7b + destroy 双 ctx（含回滚）；`0x82:0x12` 活体：READY 日志含双 ctx | dmesg 双 create 成功，零 oops | 1 次 bridge 重载 |
| r397-3 | kick Phase 2 替换（有-context 传真实 ctx）；marker 回归（r395 双 kick 模式）+ r391 V3 隔离复测 | OUT↔wire 精确匹配，dmesg 干净 | 同一重载内 |
| Phase 3 | 真实 payload（远期，需 MT_TA_VM_READY + UMD） | 另立轮次 | 待定 |

r397-1~3 可合一轮（改动集中、验证同源），由执行轮次自行拆分。

## 6. 待验证点与不确定处（诚实边界）

1. **node_type=2 的固件语义**：`mt_node_route_build` 给 dm=3，
   `capabilities=8`；真实 firmware 是否要求 TA ctx 另有 flag——无活体
   证据前，Phase 2 只用它过门禁、不依赖其提交真实负载。**[TO-VALIDATE]**
2. **In-flight destroy**：Phase 3 的 `m->context` 持有遇上
   `pvr_file_release`——`-EBUSY` 是诚实失败还是需要排空等待，
   待真实 payload 轮次设计（r374 V4 的延续）。**[TO-VALIDATE]**
3. **CSW 的 TA/3D 共用**：11 BO 含 "TA state"，CSW+0x08 指向它——
   r389 实证 3D 可用；TA 侧是否需不同 CSW 内容，待真实 TA 负载。
   **[TO-VALIDATE]**
4. 本轮纯设计，零代码改动，零硬件触碰。

## 证据索引

- `work->context` throwaway：`kernel/recovery/mt_pvr_bridge.c:4446-4452`
 （kzalloc + `route.dm=MT_FW_DM_TA`），`:4471-4484`（双路径 kfree + r368 注释）
- 门禁：`kernel/mt_marker_fence.h:419-421`
- route 表：`kernel/mt_work_command.h:93-109`（type 2→dm 3，type 5→dm 2）
- 真实路径：`mt_marker_submit_context`（`:186-206`），op 表 `:675`
- 完成双路径处理 `m->context`：`:227-233`（generic）、`:518-522`（TA）
- exec_ctx 创建/销毁：`:4308`（node_type=5）、`:4182`
- T2 白名单：`tests/test_opcode_whitelist.py`（r394）
