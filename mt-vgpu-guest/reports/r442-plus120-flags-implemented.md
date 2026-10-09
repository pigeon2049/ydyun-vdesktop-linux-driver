# r442: +0x120 flags 写入实现（离线）

> Round r442 (2026-10-09). **纯离线，零硬件触碰。** 实现 r441 的 P0：
> TA Header `+0x120` 写入 `0x1`（UMD 忠实最小值，bit0）。

## Conclusion

**`mt_ta_real_buffer_build()` 现在写入 `*(u32 *)(buf + 0x120) = 0x1`**。
r441 [MEASURED]（RGXPrepareTA, `FUN_00178800`:52118 初始化 0，:52194–52234
逐 bit OR 入 11-bit 打包）：DDK flags 全零且 `RTDataSet+0x20 == 0` 时，
UMD 的忠实最小值是 `0x1`（仅 bit0 = `(RTDataSet+0x00 & 2) == 0`，
[INFERRED 高置信] 通常为 1）。我方之前写 0，与 UMD 差 bit0。

## 实现

1. `kernel/mt_ta_real.h`：
   - 新增 `MT_TA_BUF_HDR_FLAGS 0x120U`、`MT_TA_BUF_HDR_FLAGS_MIN 0x1U`，
     附 11-bit 打包表摘要（[MEASURED] r441；DDK 源 bit 取值 [UNKNOWN]）。
   - `mt_ta_real_buffer_build()`：tile 打包写入后追加
     `*(u32 *)(buf + MT_TA_BUF_HDR_FLAGS) = MT_TA_BUF_HDR_FLAGS_MIN;`
     （4B dword，r441 反汇编为 `*(undefined4 *)(lVar8 + 0x120)`）。
   - 文档同步：非零字段现为 `+0x10`（RgnHeader VA）、`+0x50`/`+0x58`
     （tile 打包）、`+0x120`（0x1 flags）。
2. T5 白名单（`tests/ta/test_header_integrity.py`）：
   - 白名单集合加入 `MT_TA_BUF_HDR_FLAGS`（`{0x10, 0x50, 0x58, 0x120}`）。
   - 新增 `test_header_flags_constants`（常量 0x120/0x1）、
     `test_header_flags_value`（body 必须写 `MT_TA_BUF_HDR_FLAGS_MIN`）。
3. C（`tests/c/pvr_bridge_core_test.c`）：
   - `ta_hdr_written_byte()` 加入 `+0x120`（4B）范围。
   - `test_ta_real_buffer_build_target` 断言 `+0x120 == 0x1`
     （且 `== MT_TA_BUF_HDR_FLAGS_MIN`）。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**559 Python + 781 C 全绿**（Python +2 新测试；C 783->781：幂等循环跳过 +0x120 的 4B，新增 2 断言）。
- `make -C mt-vgpu-guest kernel W=1`：零警告。
- 反向验证：把 `+0x120` 写回 0 → `test_header_flags_value`
  精确 FAIL；还原 → 绿。

## 诚实边界

- bit0=1 系 [INFERRED 高置信]（RTDataSet calloc 零初始化、`+0x00` 未写入）；
  其余 10 bit 的 DDK flags 取值 [UNKNOWN]——若固件需要某 DDK bit，
  本轮的 `0x1` 仍不足，待活体验收。
- `+0x68` 仍 0（r438 P1）；`+0x138`–`+0x160` 恒零（r441 QuYuan1 [MEASURED]）。
- 本轮零硬件触碰，纯离线；活体待用户冷重启后。
