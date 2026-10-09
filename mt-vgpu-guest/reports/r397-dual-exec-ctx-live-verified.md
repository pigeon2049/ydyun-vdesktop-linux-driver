# r397: render_ctx 双执行上下文（TA/3D）落地，kick 传真实 ctx（活体验证）

## 结论

`render_ctx` 现持有**两个**真实执行上下文：`exec_ctx_3d`（node_type=5→DM2，
既有改名）+ `exec_ctx_ta`（node_type=2→DM3，新增）。TA kick（`0x82:0xC`）
有-context 时传真实 `exec_ctx_ta` 替代一次性 throwaway；无-context 时保留
throwaway 回退。活体 V1（双 ctx 创建）、marker 回归、V3（双 context 隔离）
全部通过，无 oops，dmesg 干净。

## 实现落点

### Phase 1（结构）
- `kernel/mt_render_context.h`：`exec_ctx`→`exec_ctx_3d` 改名；新增
  `exec_ctx_ta` + `exec_ta_ready`。struct 1544B→1616B。
- `tests/test_render_context_layout.py`：更新全部偏移 pin
  （exec_ctx_3d@1104、exec_ctx_ta@1176、exec_ready@1248、exec_ta_ready@1249、
  csw@1250、vm@1504、vm_base_va@1512、pt_bo@1520、resources_ready@1608）。
- `tests/test_opcode_whitelist.py`（T2）：新增
  `test_dual_exec_ctx_dm_pinning`——`exec_ctx_ta` 必须以 node_type=2 创建、
  `exec_ctx_3d` 以 node_type=5；路由表 type 2→dm 3、type 5→dm 2。
- `tests/test_render_context_create.py`：更新字段名 + 新增 TA ctx 断言。

### Phase 1（create/destroy）
- `mt_render_context_create()`：第 7b 步新增
  `mt_execution_context_create(&ctx->exec_ctx_ta, &ctx->process, 2, 0)`；
  失败走统一 `out_rollback`→`mt_render_context_destroy`。
- `mt_render_context_destroy()`：先销毁 TA ctx（`exec_ta_ready` 守卫），
  再 3D ctx + process。顺序无关（都只动 `process.contexts`）。

### Phase 2（kick）
- `pvr_cmd_musakickgfx2`：`rctx` 提升至函数作用域；有-context 且
  `resources_ready` 时 `work.context = &rctx->exec_ctx_ta`（借用，不 kfree）；
  否则 throwaway kzalloc（`route.dm=MT_FW_DM_TA`）+ 条件 kfree。
  Marker op 不拿所有权（r368），替换行为中性。

## 活体验证（bridge 重载一次，r360 流程，safe_rmmod.sh）

### V1：双 ctx 创建
```
[15324.675946] r397: exec process/contexts created (3D node_type=5, TA node_type=2)
[15324.675947] r389: render context READY (11 BOs, CSW, exec)
```
- `0x82:0x12` → handle=0x1000，error=0，无 oops。

### Phase 2 回归：TA 双 kick
- KICK[ctx=0x1000] → `r397: kick with real exec_ctx_ta (dm=3)` →
  `submitted wire=6` ↔ OUT.update_fence=6 ✅
- KICK[no-ctx] → throwaway 回退 → `submitted wire=7` ↔ OUT.update_fence=7 ✅

### V3：双 context 隔离
- CREATE 0x1000/0x1001（各 11 BO 真实 context）
- KICK[0x1000] → fence=8，KICK[0x1001] → fence=9；两次均为
  `kick with real exec_ctx_ta (dm=3)`，按 handle 正确路由
- 文件关闭后 probe ref 25→13（r390 V2b 路径），无泄漏
- dmesg 零 WARN/BUG/Oops

## 门禁
- `make -C mt-vgpu-guest check-offline`：**474 Python + 299 C 全绿**
  （+2 新测试：layout 更新、T2 dual pinning）
- `make kernel` W=1：**零警告**
- 反向验证：TA node_type 2→5 → T2 FAIL；layout 偏移改错 → FAIL；恢复 → 全绿

## 安全合规
- Pre-live 门禁 T1/T2/T3 全过后再 live
- 一次 bridge 重载（safe_rmmod.sh，refcount=0 确认）；未重启；`rmmod -f` 零出现
- `timeout` 未进临界区；两端 /tmp 零残留

## 诚实边界
- Marker 级验证（零绘制）；`MT_TA_VM_READY` 门保持关闭
- `exec_ctx_ta` 仅用于过门禁（`route.dm==3`），未提交真实 TA 负载
  （Phase 3，需 `MT_TA_VM_READY` + UMD payload，另立轮次）
- node_type=2 的固件语义（capabilities=8 之外）待真实负载验证 [TO-VALIDATE]
- In-flight destroy（`active_jobs!=0` 时 `-EBUSY`）未活体验证 [TO-VALIDATE]

## 证据
- `reports/r397-dmesg-dual-exec-ctx.txt`（0600）：15 行关键 dmesg
