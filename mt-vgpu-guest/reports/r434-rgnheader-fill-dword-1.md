# r434: RgnHeader 填充修正为逐 dword 写 0x00000001（离线）

## 结论
r433 的反汇编纠正落地：`InitRegionHeaderBuffer` 逐 dword 写整数 `1`
（`*local_690[0] = 1`，`undefined4*` [MEASURED]），r430/r431 的
`0xFFFFFFFF` 是误读。本轮将内核实现与测试同步到正确值。

## 代码修正
1. `kernel/mt_ta_real.h`：
   `MT_TA_RGNHEADER_INIT_DWORD` 由 `0xFFFFFFFFU` → `0x1U`；
   注释明确"each dword = integer 1"（[MEASURED] r433，corrects r430/r431）。
   注：文件内 RgnHeader 大注释块本就写"pre-fills every dword with 1"，
   只有该 define 的值错了。
2. `kernel/recovery/mt_pvr_bridge.c`（`mt_render_context_create`，r431 块）：
   删除 `memset(rgn_init, 0xFF, sizeof(rgn_init));`，
   改为逐 dword 循环 `rgn_dw[i] = MT_TA_RGNHEADER_INIT_DWORD;`
   （`u32 *rgn_dw = (u32 *)rgn_init`；复用函数内已声明的 `u32 i`，
   其前序用途在 BO 创建循环，已结束，无冲突）。
   块注释同步更新为"Pre-filled per-dword with 0x00000001 …
   [MEASURED] r433, corrects r430/r431"。
3. `tests/c/pvr_bridge_core_test.c`（`test_ta_rgnheader_init_pattern`）：
   模拟内核填充改为逐 dword 写 `1U`；
   断言 `MT_TA_RGNHEADER_INIT_DWORD == 0x1U`；
   新增边界：`!= 0xFFFFFFFFU`（防 r431 误读重演）。

## Python 门禁同步
`tests/ta/test_header_integrity.py::TestRgnHeaderWiring::test_rgnheader_init_all_ones`
曾断言源码含 `memset(rgn_init, 0xFF, ...)`（r431 行为），check-offline
首轮即精确 FAIL 拦截了本轮改动——按 r434 更新为：
- `assertIn("rgn_dw[i] = MT_TA_RGNHEADER_INIT_DWORD;")`
- `assertNotIn("memset(rgn_init, 0xFF, sizeof(rgn_init));")`

## 反向验证
临时将内核写回 `memset(rgn_init, 0xFF, ...)` → 上述 Python 测试精确 FAIL
（"r434 FAIL: RgnHeader BO must be filled per-dword with 0x00000001"）；
还原后全绿。

## 门禁
- `make -C mt-vgpu-guest check-offline`：**554 Python + 1491 C 全绿**
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 本轮零硬件触碰，纯离线。

## 诚实边界
- RgnHeader 语义仍 [INFERRED] 未升 [MEASURED]（r432 证伪性证据：0xFF 填
  充下固件仍超时；0x00000001 填充尚未活体验收）。
- per-dword 值 1 的行为 [MEASURED]（r433 反汇编），但该值是否让固件
  完成仍需活体验证（r435+，需用户冷重启）。
- 本轮生产代码变更仅限填充值；默认门控无变化。
