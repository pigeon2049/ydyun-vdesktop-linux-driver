# r423：Header-only 方案落地——Entry 不再写 buf+0，T5 门禁拦截污染

> 轮次：r423（2026-10-09）。**纯离线。** r422 根因的修复。
> 背景：r422 定位 r421 超时根因为 Entry 写 buf+0 污染 Header（0x10/0x18/0x20）；
> 360B = Header(0x00-0x160) + Entries 双区；Entries 不在 360B 内。

## 结论

**`mt_ta_real_buffer_build()` 改为 Header-only：不再写 Entry，僅置 `TA_buf+0x10 = target_va`（→psKickTA[1] render target，[MEASURED] r422），其余全零。新增 T5 Header 完整性门禁，永久拦截 Entry 污染。**

## 实现

### 1. `kernel/mt_ta_real.h`
- 新增 `MT_TA_BUF_HDR_TARGET_VA 0x10U`（[MEASURED] r422）。
- 新增 Header 双区结构文档（0x00-0x160 Header，Entries 不在 360B 内）。
- `mt_ta_real_buffer_build()` 重写：
  - `n_entries` 必须为 0（Header-only）；非零 → `-EINVAL`。
  - `memset` 清零后，`*(u64*)(buf + 0x10) = target_va`。
  - 删除 Entry 写入循环（r422 污染源）。

### 2. `kernel/recovery/mt_pvr_bridge.c`
- `mt_ta_submit_real()`：`n_entries` 验证改为 `!= 0` → `-EINVAL`
  （Header-only；Entries 容器未知）。

### 3. T5 门禁：`tests/ta/test_header_integrity.py`（新，5 tests）
- `test_no_entry_struct_in_buffer_build`：ban `struct mt_ta_entry_simple` in buffer_build。
- `test_no_entry_offset_arithmetic`：ban `(buf + i *` 模式。
- `test_header_target_va_set`：Header+0x10 必须由 target_va 设置。
- `test_n_entries_must_be_zero`：`n_entries != 0` 必须被拒。
- `test_header_constant_defined`：`MT_TA_BUF_HDR_TARGET_VA == 0x10`。
- **反向验证**：注入 `(struct mt_ta_entry_simple *)buf` → T5 精确 FAIL；还原 → 绿。

### 4. 存量测试更新
- `tests/c/pvr_bridge_core_test.c`：`test_ta_real_buffer_build_target()` 重写为 Header-only 语义
  （n_entries=0 有效；1/9/10 无效；+0x10 为 target_va，其余零）。
- `tests/ta/test_ta_readback.py`：`test_submit_real_validates_n_entries` 更新为 `!= 0` 检查。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**548 Python + 1054 C 全绿**（1 skipped）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 反向验证：T5 注入污染 → FAIL；还原 → 绿

## 诚实边界

- Entries 真实容器仍未知（544B 推断）；Header-only 是最小风险方案。
- Q1=target_va（Entry 内）仍 [INFERRED]；本轮未动 Entry 语义。
- `w`/`h` 参数保留验证（API 稳定），Header-only 下未使用。
- 本轮零硬件触碰，纯离线；r424 活体前需双门控重建。

## 下一步

r424（活体）：双门控重建 + `mt-ta-readback`，验证 Header-only（`TA_buf+0x10=target_va`）
是否被固件接受（0x100）且 Header 不再污染。
