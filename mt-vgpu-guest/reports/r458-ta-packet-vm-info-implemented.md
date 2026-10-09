# r458：TA 包 VM 信息修复实现（+0x18 root_pa、+0x20 token）

**结论：r457 定位的 TA DM 包 VM 信息缺失已修复。`struct mt_ta_submit_params` 新增 `vm_root_pa`/`vm_token`，`mt_ta_submit_real` 从 `exec_ctx_ta.process` 填写，`mt_fw_ta_real_command` 写包 `+0x18`/`+0x20`。11 个新测试，反向验证 10/11 精确 FAIL（第 11 个验证观察者路径保持 0，符合预期）。门禁 609 Python + 851 C 全绿，kernel W=1 零警告。纯离线，零硬件触碰。**

## 实现内容

### 1. `kernel/mt_ta_submit.h`

`struct mt_ta_submit_params` 追加（ABI append，不破坏现有字段偏移）：
```c
u64 vm_root_pa;  /* r458: GPU page-table root PA for TA VA translation (+0x18) */
u64 vm_token;    /* r458: process token identifying the GPU VM (+0x20) */
```

布局 pins 更新（r364 gate）：
- `sizeof` : 104 → **120**（+16）
- 新增 `vm_root_pa @104`、`vm_token @112` static_assert
- `check_fence @96` 不变

`mt_ta_params_from_musakickgfx2`（0x82:0xC 观察者路径）使用 `{0}` 初始化，新字段保持 0——不执行，不填。

### 2. `kernel/recovery/mt_pvr_bridge.c`（`mt_ta_submit_real`）

```c
/* r458: TA DM packet needs GPU VM info (+0x18 root_pa, +0x20 token)
 * so firmware can translate TA/RgnHeader VAs. Same expressions as
 * mt_execution_context_inputs(). exec_ctx_ta.process is non-NULL
 * (validated by mt_execution_context_create when exec_ta_ready). */
work.params.vm_root_pa =
    rctx->exec_ctx_ta.process->vm->tables->backing.gpu_pa;
work.params.vm_token = rctx->exec_ctx_ta.process->token;
```

安全性：`exec_ta_ready` 为真意味着 `mt_execution_context_create` 成功（`process` 非空），且 `mt_execution_process_create` 成功（`vm`/`vm->tables` 非空）。与 `mt_execution_context_inputs` 使用完全相同的表达式。

### 3. `kernel/mt_marker_fence.h`

`mt_fw_ta_real_command` 新增 `u64 root_pa, u64 token` 参数：
```c
mt_fw_put64(command, 0x18, root_pa);
mt_fw_put64(command, 0x20, token);
```
对齐 `mt_work_command_encode()` 参考布局（r457）。

`mt_ta_submit_build` 透传 `params->vm_root_pa`/`params->vm_token`。

## 新测试

**`tests/ta/test_ta_vm_info.py`**（11 tests，5 类）：
1. `TestTaSubmitParamsVmFields`（3）：字段存在性、类型、append 位置
2. `TestTaSubmitRealFillsVmInfo`（3）：赋值点存在性、`exec_ctx_ta.process` 表达式
3. `TestTaRealCommandWritesVmInfo`（3）：签名含 root_pa/token、`+0x18`/`+0x20` 写入
4. `TestTaSubmitBuildPassesThrough`（1）：透传
5. `TestMusakickgfx2PathStaysZero`（1）：观察者路径保持 0

**`tests/ta/test_ta_submit_layout.py`** 扩展：`sizeof_params` 104→120，新增 `off_vm_root_pa=104`、`off_vm_token=112`。

## 反向验证

- 新代码：11/11 通过
- 回退 kernel 改动（stash）：**10/11 精确 FAIL**（第 11 个 `test_zero_init_covers_new_fields` 通过符合预期——验证观察者路径行为保持，旧代码本就满足）
- 恢复后：11/11 通过

## 门禁

- `make -C mt-vgpu-guest check-offline`：**609 Python OK**（598+11 新，1 skipped）+ **pvr_bridge_core_test OK (851 checks)**
- `make -C mt-vgpu-guest kernel W=1`：**零警告**

## 诚实边界

- [MEASURED]：包字段对比；`build_command` 签名无 context；三处代码改动。
- [INFERRED 高]：缺失 VM 信息 → MMU fault → hang（r457）。
- [UNKNOWN]：host proxy 实际处理；固件 hang 精确位置。
- **待 r459 活体验**：若 MMU fault 是根因，应看到行为变化（完成或新错误码，而非 5s 超时）。
- 本轮纯离线，零硬件触碰；**未链入下一轮**。
