# r90：shim 补 0x89:0x5 非零句柄，Rogue2D 再进两桥

r87 卡点的 fabricated 侧解法落地：shim 给 `0x89:0x5` 回非零句柄
（`0xa000+` 计数器，eError 保持 0），Rogue2D spike 从 95 条走到
100 条——越过 GetSharedMemory，进入 `TQPMR_MapMem`
（`0x6:0x3` MakeLocalImport → `0x6:0x6` Import），随后因 import
路径的 UMD 侧校验未过而 unwind（`0x6:0x7` + `0x89:0x6`×2 + 全拆除，
依然干净）。代码提交 `e0b1d2c`（22 行，`-Werror` 干净；
行为即反向验证：零值停滞 vs 非零前进）。全程离线，零硬件触碰。

## 精确定位（本轮终点 = 下一轮起点）

- 新卡点不在桥里：shim 对 `0x6:0x6` 回 `ret=0`，但 UMD 没调
  `0x6:0x4`（Unmake），直接 `0x6:0x7`（Unref）走人——说明
  `MTSRVDevmemLocalImport → FUN_00197100 → FUN_00197500 →
  FUN_00197870 → FUN_00132200` 链中某处校验了 OUT 内容并判负。
- 嫌疑：canned `0x6:0x6` OUT（对齐/句柄/错误码槽位与 UMD 期望错位；
  注意 rung8 同 OUT 却能过——TQPMR 路径查了别的字段）。
- 下一步：跟 `FUN_00197500/00197870/00132200` 三层，定位拒绝字段；
  修 shim canned 或确认要真值。二选一，不盲调。

## 附带

- `0x82` 系在本轮 trace 中零出现——Rogue2D（TDM）与 TA（0x82:0xc）
  是两条独立绘制路径，互不前置。T3（TA）与真绘制（TDM）可并行推进，
  谁先出包谁赢。
- 证据：`r90-rogue2d-past-shmem.jsonl`（100 条，`0x89:0x5 OUT=00a0…01a0…`）。
