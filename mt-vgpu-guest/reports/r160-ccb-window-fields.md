# r160：SubmitTransfer3 CCB 窗口 39B 的偏移分布与生成器字段对照

- **结论**：`0x8000f44000`/`0x1200` 窗口的 39 个非零字节已全部定位（27 runs，单轮 trace 内自洽）。其中 `+0x10` 的 u64 = `CCB+0x58`、`+0x28` 的 u32 = `0x1078`（= `0x58+0x1020`），与语料 `SubmissionCmdGenerate`（`decompiled.c:37014`，SHA 已核 `b3058c02…`）的两次定长拷贝在形状上吻合：头 `0x58B ← job+0x0`、体 `0x1020B ← job+0x58`。`+0x40` 的 2B 三轮各异（其余 37B 三轮逐字节一致），来源未定，候选 ASLR 指针或未初始化内存，留待离线 GDB 观察点确认。本轮零硬件触碰。

## 实测

1. shim `ccb_resolve` 加 `runs`（非零 runs 的 offset + ≤64B hex，上限 32 runs；合成门禁断言首 run 形状）。`musa_blit_test -device 0 -f -o`（major 2 + shared backing，`timeout -s KILL 15`）单轮 trace 528 行、1 条 `ccb_resolve`，27 runs 恰好覆盖 39B。完整证据见 [`r160-ccb-window-map.jsonl`](r160-ccb-window-map.jsonl)。
2. 窗口字段表（offset 相对 CCB 首字节；三轮一致者注明，否则标变）：

   | 偏移 | 内容 | 生成器对照 |
   |---|---|---|
   | `+0x00–0x0F` | 全零 | 头部空位（job 新结构体，仅少数欄位赋值） |
   | `+0x10` | u64 `0x8000f44058` = CCB+`0x58` | `*(job+0x10) = CCB+0x58` 写回形状吻合 |
   | `+0x1C` | u32 `0x67`（103） | 头部，含义未定，只记录值 |
   | `+0x28` | u32 `0x1078` = `0x58+0x1020` | 两次定长拷贝的总基线长度，吻合 |
   | `+0x2C` | u32 `1` | 头部，含义未定 |
   | `+0x40` | 2B，三轮 `fa24/2d25/3425` 各异 | **未定**（下见） |
   | `+0x58` | `02` | 体拷贝起点（`← job+0x58` 首字节） |
   | `+0x1060` | u16 `0x1020`（= `0x1078−0x58`） | 体尾部观测值，不展开解释 |
   | `+0x1078–0x11B8` | 约 25B 稀疏（`03 … 9010 … 0801/d007 … 0c08`） | 扩展区/region 条目形状；条数组合算术未闭合（`0x80×1+0x18×11` 恰好凑满但无旁证，不断言） |

3. `FUN_0015f890`（`decompiled.c:37335`，即 `TQSubmissionSubmit`）确认提交侧链条：`SubmissionSetCheckSyncPrim`（`flag&1`）/ `SubmissionSetUpdateSyncPrim`（`flag&2`，与 r159 一致）编组后调 `BridgeRGXTDMSubmitTransferDDK2`；本轮 Submit3 的 `update_count=2` 与两条 `flag&2` 条目一致，update 数组走 IN 指针、不在 CCB 窗口内。
4. `make -C mt-vgpu-guest check-offline` 全绿（270 Python，1 skip + 272 C；含 `runs` 断言的 `test_pvr_shim_ccb_resolve`）；shim `-Werror` 通过；`lsmod` 无 `mt_*`。

## 边界

- 吻合≠证实：`+0x10`/`+0x28` 的形状证据支持“该窗口是 `SubmissionCmdGenerate` 输出”，但字段语义（`0x67`、`0x2C=1`、扩展区条目类型）仍需更多 producer 输入交叉，不在本轮断言内。
- `+0x40` 的轮间差异只在 2B 上，其余 37B（含 FNV 外的全部内容）三轮一致；差异源未定，不断言为指针或计数器。
- shim 回包仍 fabricated；trace 不证明 bridge 接受或 GPU 执行；不重载硬件 bridge。

## 下一步

- 离线 GDB 对 `SubmissionCmdGenerate` 的 `param_2+0x40` / CCB `+0x40` 下写观察点（`UMD_TRAP` + 只读，不碰硬件），确定该 2B 的写入者与轮间变化原因。
- 换 producer（不同 blit 参数/尺寸）看窗口长度与扩展区是否跟随变化，再闭合条目算术。
