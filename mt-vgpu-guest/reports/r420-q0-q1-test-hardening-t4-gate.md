# r420：Q0/Q1 修正测试加固 + T4 纯净性门禁（离线）

> 轮次：r420（2026-10-09）。**纯离线。**
> 背景：用户指示"继续 加更多测试和门禁"——r420 活体前继续加固。
> r419 证明 Q0 是纯 flags、Q1 才是 48 位地址；r418 把 VA 塞进 Q0 导致固件超时。

## 结论

**新增 21 Python 测试 + T4 安全门禁；修复 1 处 stale 文档；门禁 543+630 全绿。**

## 新增测试

### 1. T4 门禁：`tests/ta/test_q0_purity.py`（3 tests）
- `test_q0_no_va_or_in`：源码扫描 `mt_ta_real.h`/`mt_marker_fence.h`，禁止 Q0 赋值 RHS 出现 `| va`/`| addr`/`+ va` 模式（r418 重演防护）
- `test_q0_constant_is_flags_only`：`MT_TA_ENTRY_Q0_FLAG_BITS` 必须恰为 bits 39+42，低 32 位为 0
- `test_q1_gets_va`：`e->q1 = target_va & 0xFFFFFFFFFFFFULL` 必须存在

**反向验证**：注入 `e->q0_addr_flags = target_va | MT_TA_ENTRY_Q0_FLAG_BITS` → T4 **FAIL**（定位到行号）；还原后全绿。

### 2. Q0/Q1 位域测试：`tests/ta/test_q0_q1_bitfields.py`（15 tests）
- `TestQ0FlagBits`（4）：常量值、bits 39/42 独立、低 32 位为 0、bits 29/30 保持 clear
- `TestQ1MaskBoundaries`（5）：0、全 48 位、49 位截断、典型 VA、源码 mask 存在
- `TestQ0Q1Combination`（3）：Q0 恰为常量、Q1 为 mask 后的 VA、Q0 表达式无 VA
- `TestThreeStateHistory`（3）：r414（Q0=0 可接受）、r418（污染形状禁止）、r419（当前正确态）

### 3. DM 布局回归：`tests/ta/test_ta_real.py::TestTaDmLayoutUsage`（3 tests）
- 常量被实际使用（非仅定义）：`mt_fw_ta_real_command` 内必须引用 `MT_TA_DM_PKT_TA_VA_LO/HI/SIZE`
- 禁止硬编码 0x28/0x2c/0x30（必须用命名常量）
- `mt_marker_fence.h` 注释不再含 `[INFERRED]: TA buffer VA`（已更新为 [MEASURED]）

## 文档修复

`kernel/mt_marker_fence.h` 的 Real TA 注释块仍标 `[INFERRED]`（r414 已实测、r415 已在 `mt_ta_real.h` 更新，此处遗漏）。已改为 `[MEASURED]`（r414 live: 0x100/219us）。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**543 Python + 630 C 全绿**（1 skipped）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 反向验证：T4 注入污染 → FAIL；还原 → 绿

## 交付物

- 本报告 `reports/r420-q0-q1-test-hardening-t4-gate.md`
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r420（§4）
- `PROGRESS-SNAPSHOT.md` §12 追加 r420
- 本地提交（不 push）

## 诚实边界

- Q0 各 flag 位语义（除 29/30/39/42）仍未知；测试只 pin"按设计行为"
- Q1 = render target 仍为推断，待活体验（r420+）
- 本轮零硬件触碰，纯离线
