# r398: R5 Phase 2 完成——per-file VM 回退删除，per-context 为唯一路径（离线）

## 结论

R5 per-file VM（`file->ta_vm_ctx`）已彻底删除。`pvr_cmd_musakickgfx2` 无有效
render_ctx 时直接返回 `-EINVAL`，不再回退。per-context VM（r389/r391）为唯一
路径。门禁 474+299 全绿，`make kernel` W=1 零警告，反向验证通过。零硬件触碰。

## 删除清单

| 位置 | 内容 |
|---|---|
| `mt_pvr_bridge.c:353` | `struct mt_pvr_file` 的 `ta_vm_ctx` 字段 + 注释 |
| `mt_pvr_bridge.c:893` | `mt_bridge_ta_vm_create/destroy` 前向声明 |
| `mt_pvr_bridge.c` `pvr_file_release` | R5 per-file VM 销毁块 |
| `mt_pvr_bridge.c:3990` | r376 注释 + `MT_BRIDGE_TA_VM_PT_*` defines |
| `mt_pvr_bridge.c:4006` | `mt_bridge_ta_vm_create()`（~50 行，synthetic BO） |
| `mt_pvr_bridge.c:4058` | `mt_bridge_ta_vm_destroy()`（~12 行） |
| `mt_pvr_bridge.c:47` | `#include "../mt_ta_vm.h"`（已无使用者） |
| `kernel/mt_ta_vm.h` | 整个文件（r374 R5 接口头，无引用者） |
| `tests/test_ta_vm_impl.py` | 死亡测试（r375 符号，早已不在门禁） |
| `tests/test_ta_vm_layout.py` | 死亡测试（pin 已删除的 header） |

**保留**：`struct mt_bridge_ta_vm`（`mt_render_context.vm` 字段类型；
`mt_render_context_vm_create/destroy` 的载体）。

## Kick 路径变更（Phase 2）

**VM 选择**：删除 `kick_vm` 中间变量与 per-file fallback 分支。无
`rctx && rctx->resources_ready && rctx->vm` 时直接 `return -EINVAL`
（此前在 `pvr_session_acquire` 之前，无锁、无需清理，模式同 D5 的
`-EOPNOTSUPP`）。有则直接对 `&rctx->vm->vm` 做 V2 空绑定验证。

**exec_ctx 选择**：删除 throwaway `kzalloc` 分支与 `use_real_ctx` 变量。
`ctx = &rctx->exec_ctx_ta`（借用）；删除两处 `if (!use_real_ctx) kfree(ctx)`
（借用指针不可 free，否则 corrupt render context）。

## 测试更新

| 文件 | 变更 |
|---|---|
| `test_kick_render_ctx.py` | `test_falls_back_to_per_file` → `test_rejects_without_live_render_ctx`（断言无 `ta_vm_ctx` 引用、有 `-EINVAL`、有 `r398 Phase 2` 标记）；`test_uses_per_context_vm_when_ready` 改断言 `&rctx->vm->vm`；`test_reverse_validation` 改查 `r398 Phase 2` |
| `test_probe_ta_vm.py` | `test_bridge_proper_init` 改为 Phase 2 语义：`mt_gpu_vm_init(&tvm->vm` 仍在（per-context），断言无 `mt_bridge_ta_vm_create()`、无 `Synthetic BO` |
| `test_ta_kick_ctx_release.py` | 语义反转：r369 的"kfree 义务"只适用于 dispatch-allocated throwaway；r398 后 ctx 为借用，断言**无** `kfree(ctx)`（error/success 路径）+ 无 `kzalloc(sizeof(*ctx)` |
| `test_ta_completion_path.py` | 正则锚点从 `kfree(ctx);` 改为 `dma_fence_put(fence);` |

## 门禁

- `make -C mt-vgpu-guest check-offline`：**474 Python + 299 C 全绿**
- `make kernel` W=1：**零警告**
- 反向验证：注入 `file->ta_vm_ctx` 引用 → `test_rejects_without_live_render_ctx` FAIL；还原 → 全绿

## 诚实边界

- 本轮纯离线删除 + 测试更新，零硬件触碰，未活体验证
- 下轮 live 需验证：无 ctx 的 `0x82:0xC` 返回 `-EINVAL`（r395 的 harness 传 `ctx=0x0` 会走到新分支）
- `MT_TA_VM_READY` 门保持关闭；marker 级路径不变（有 ctx 时）
- 运行中的 bridge 仍是 r397 构建（含 per-file 回退），下次重载后生效
