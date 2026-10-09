# r455：RgnHeader 双循环初始化已实现——128 dwords 对齐 UMD（选项 A 精炼版）

**结论：`MT_TA_RGNHEADER_INIT_BYTES = 2 × MT_TA_RGNHEADER_BYTES`（0x200U）已实现并测试。**
**r431–r454 的 64-dword 半量初始化成为历史。纯离线轮，零硬件触碰。**

## 选项决策

r454 [MEASURED]：UMD `InitRegionHeaderBuffer` 做两次循环，每次写 `local_700`
dwords 的 `1` 到**同一 advancing pointer**（总计 2×local_700 dwords，连续内存）。

| 选项 | 方案 | 结论 |
|---|---|---|
| A（任务原文） | `MT_TA_RGNHEADER_BYTES` 0x100→0x200，单循环写 128 dwords | 功能正确，但破坏 `mt_ta_rgnheader_size(64,64)==MT_TA_RGNHEADER_BYTES` 不变量 |
| B | 保持 256B BO，双循环覆盖写 | 不合理（第二次覆盖第一次） |
| C | 512B BO，双循环各写 256B | 最忠实 UMD，但两循环在 UMD 里是实现细节 |
| **A-精炼（采用）** | 保留 `MT_TA_RGNHEADER_BYTES=0x100U`（逻辑尺寸），新增 `MT_TA_RGNHEADER_INIT_BYTES=(2U*MT_TA_RGNHEADER_BYTES)`；bridge 用 INIT_BYTES | **采用** |

**采用 A-精炼的理由**：
1. UMD 的两次循环写向同一 advancing pointer，是**连续**的 2×local_700 dwords
   写入——单循环写 128 dwords 功能完全等价，两循环结构只是实现细节。
2. `mt_ta_rgnheader_size()` 返回逻辑尺寸（64×64→0x100），现有测试断言
   `mt_ta_rgnheader_size(64,64)==MT_TA_RGNHEADER_BYTES`；直接改 define 会破坏
   该不变量，独立的 INIT_BYTES define 语义更清晰。
3. BO 本来就是 PAGE_ALIGN（4096B），VA stride 16MB——512B 写入无任何分配影响。

## 实现内容

**`kernel/mt_ta_real.h`**：
- 新增 `MT_TA_RGNHEADER_INIT_BYTES (2U * MT_TA_RGNHEADER_BYTES)`（0x200U），
  附 r454 [MEASURED] 注释。
- `MT_TA_RGNHEADER_BYTES=0x100U` 保持不变（逻辑尺寸）。

**`kernel/recovery/mt_pvr_bridge.c`**（RgnHeader 13th BO 创建块）：
- `rgn_alloc = PAGE_ALIGN(MT_TA_RGNHEADER_INIT_BYTES)`（仍 4096B）
- `u8 rgn_init[MT_TA_RGNHEADER_INIT_BYTES]`（栈缓冲 256→512B）
- 填充循环 `MT_TA_RGNHEADER_INIT_BYTES / sizeof(u32)`（64→128 dwords）
- `pvr_translator_bo_write(..., MT_TA_RGNHEADER_INIT_BYTES)`
- 附带修复：`"0x100B"` 注释笔误（r454 确认）→ 0x100。

## 新测试

**C**（`tests/c/pvr_bridge_core_test.c`）：
- `test_ta_rgnheader_init_pattern`：更新为 INIT_BYTES（128 dwords 逐字验证）。
- 新增 `test_ta_rgnheader_init_bytes`：INIT_BYTES==0x200U、==2×BYTES、
  逻辑尺寸不变（`mt_ta_rgnheader_size(64,64)==0x100U`）。

**Python**（`tests/guest/test_rgnheader_double_init.py`，9 tests）：
- define 关系 + r454 引用 + 逻辑尺寸不变（3）
- bridge 四处使用 INIT_BYTES：alloc/栈缓冲/循环/写（4）
- 旧半量模式已清除 + `0x100B` 笔误已修复（2）

## 反向验证

- 新代码：9/9 通过。
- 回退 kernel（stash，测试文件保留）：**8/9 精确 FAIL**
  （第 9 个验证逻辑尺寸不变，旧代码本就通过——符合预期）。
- 恢复后：9/9 通过。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**598 Python + C 全绿**
  （589+9 新；pvr_bridge_core_test OK，851 checks）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**

## 诚实边界

- [MEASURED]：UMD 双循环写 2×local_700 dwords；我方现写 128 dwords。
- [INFERRED]：半量初始化是固件 5s 超时的根因——待活体验（r456）。
  r451 已证实 13 BO 绑定、context READY 但固件无响应；若后一半未初始化
  内存被固件读取为 region header，解析垃圾→hang 与观测一致。
- [UNKNOWN]：固件实际读取 RgnHeader 的字节数；双循环的真实用途
  （双缓冲 vs 大 buffer）。
- 本轮零硬件触碰；生产代码仅 RgnHeader 初始化路径变更；**未链入下一轮**。

## 下一步

- **r456**（待用户冷重启，第 15 次）：trial 重建 → `+0x120=0x1` + 128-dword
  RgnHeader 活体，验证固件是否完成。
