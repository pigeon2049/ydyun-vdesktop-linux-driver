# r424：Header-only 边界加固 + T5 门禁增强（离线）

> 轮次：r424（2026-10-09）。**纯离线。** 用户指示"加门禁 加测试 稳妥推进"。
> 背景：r423 落地 Header-only（仅 `TA_buf+0x10=target_va`）+ T5 门禁（5 tests）；
> 本轮在活体前继续加固边界与门禁。

## 结论

**Header-only 边界行为全部锁定测试，T5 升级为白名单制：`buffer_build` 内除 `MT_TA_BUF_HDR_TARGET_VA` 外禁止任何 `buf+offset` 写入（<0x160）；`mt_ta_submit_real()` 禁止直接写 `ta_buf`。门禁 550 Python + 1416 C 全绿，kernel W=1 零警告。**

## 1. Header-only 边界测试（C，`test_ta_real_buffer_build_target` 扩展）

| 边界 | 行为 | 状态 |
|---|---|---|
| `target_va=0` | Header 全零（r414 "无工作"语义） | 已有，保留 |
| `w=0x8000,h=0x8000` | 接受（与 `mt_ta_entry_simple_build` 一致） | 新增 |
| `w=0x8001` 或 `h=0x8001` | `-EINVAL` | 新增 |
| `target_va=0x7b000001`（非对齐） | 原样存储（无对齐强制） | 新增，[TO-VALIDATE] |
| `target_va=0x1000000000000`（>48 位） | 原样存储（无 mask） | 新增，[TO-VALIDATE] |
| 连续两次 build（不同 va） | 后一次完全覆盖（memset 幂等） | 新增 |
| `buf=NULL` / `n_entries!=0` | `-EINVAL` | 已有，保留 |

**设计决策**（稳妥推进）：不对非对齐/超 48 位 `target_va` 加新拒收——
无固件依据，贸然加校验可能误杀合法用例；行为已测试锁定，
语义标 [TO-VALIDATE] 待活体。

## 2. T5 门禁增强（Python，`test_header_integrity.py` +2）

- `test_header_write_whitelist`：扫描 `buffer_build` 体内所有
  `buf+offset` 写入；白名单仅 `MT_TA_BUF_HDR_TARGET_VA`；
  任何其他数值 offset < 0x160（Header 全区）→ FAIL。
  （r423 的 ban 针对 Entry struct/算术模式；本轮升级为
  地址白名单，覆盖未来任何形式的 Header 写入。）
- `test_submit_real_no_direct_buf_writes`：扫描
  `mt_ta_submit_real()` 体，禁止 `ta_buf[` 直接写入——
  所有 Header 字节必须经 `mt_ta_real_buffer_build()`，
  submit 路径只做 `pvr_translator_bo_write` 整块拷贝。
- **反向验证**：注入 `*(u64*)(buf+0x28)=0xdeadbeef` →
  whitelist 测试精确 FAIL；还原 → 全绿。

## 3. Header 字段完整性

除 `+0x10` 外的 Header 字段（`+0x28/+0x30/+0x68` 等，r422 [MEASURED]
存在）：值未知，**不猜测**，保持零。文档已在 `mt_ta_real.h`
标注 [TO-VALIDATE]（r423）。本轮不做变更。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**550 Python + 1416 C 全绿**（1 skipped）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 反向验证：T5 白名单注入污染 → FAIL；还原 → 绿

## 诚实边界

- `target_va` 非对齐/超 48 位行为为"原样存储"，固件语义未知。
- Entries 真实容器仍未知；Header-only 仍是最小风险方案。
- 本轮零硬件触碰，纯离线；r425 活体前需双门控重建。

## 下一步

r425（活体）：双门控重建 + `mt-ta-readback`，验证 Header-only
（`TA_buf+0x10=target_va`，零 Entries）是否被固件接受（0x100）。
