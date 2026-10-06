# r188：保留项全抽取——55 功能号 + PMR 尺寸 + stream/slot（零硬件触碰）

- **结论**：r187 故意保留的 6 类中，可抽取的已全部抽取：
  dispatch 55 标签命名进 `mt_pvr_wire.h`（`MT_PVR_FN_*`，值出自
  requirements 表 + wire 注释；`0x89:0xa` 按 SUBMITTRANSFER2 模式派生，
  已注明）、PMR 三尺寸进 bridge 顶部、stream/slot 三宏进 addr_plan
  （含 readback 除数）。ioctl 号（static_assert 在位）与 param 默认
  （静态零值 +逐项文档）经复核无可抽之处，如实保留。
  门禁 292+292 全绿，`W=1` 零警告；反向验证通过。

## 实测

1. 功能号全表：8 组 55 标签逐组计数替换（12/5/16/2/6/7/2/5），
   `case 0x` 在 dispatch 内零残留（门禁断言）。
   R2 的三宏改全名以统一（`RGXKICKSYNC2` 等），r187 旧测试同步。
2. 未分发 ID 不命名：`0x89:0x0`（table 有名但无 case，直落 default）
   的宏已删——表头注释“仅命名分发处理的 ID”即此意。
3. PMR 尺寸：`MT_PVR_SYNC_BLOCK_BYTES` / `MT_PVR_HWPERF_PMR_BYTES` /
   `MT_PVR_TDM_SHMEM_BYTES`（bridge 顶部，5 处引用）。
4. stream/slot：`MT_TQX_STREAM_{SRC,DST}_VA` /
   `MT_TQX_STREAM_SLOT_BYTES`（addr_plan 新增；5 live 文件 24 处引用 +
   readback 除数，语义原样保留）。
5. 门禁：新增 `test_pvr_fn_ids.py` 2 项（55 值全钉 + dispatch 全覆盖）；
   15 个旧测试按旧字面量断言路由，同步到宏形式（意图不变）。
   全量 `check-offline` 292 Python + 292 C 全绿。
6. 反向：`MT_PVR_FN_RGXKICKSYNC3` 改 `0x6` 即红，还原即绿。

## 过程记录（诚实）

- 批量替换误伤两处：`case\s+0x3:` 同形两处（make_import 与 zs-destroy），
  第二处错命名后已纠正；hwperf 测试的 `0x5` 首轮错配 ACQUIRE（应为
  RELEASE），已纠正。教训：同形模式必须逐处点名，不批量。
- session_ops 新测试首轮误报（宏定义自身含字面量），改为“恰一次”断言。

## 边界

- 纯重命名/宏引用重构，数值逐位不变（门禁钉死），无行为变化；
  未碰模块加载与会话。

## 下一步（候选）

- 活体项仍待批：kill-while-busy 关账 / TQX 真发射 / `=2` update 验证。
