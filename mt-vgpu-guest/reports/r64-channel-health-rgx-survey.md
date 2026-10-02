# r64：新会话通道健康 + DMA 回读重验 + RGX 路径勘测

经用户授权真机测试。在当前 retained 会话上重建最小执行证据，
再离线勘测 RGX 提交路径，为 translator T3 定序。

## 活体（均绿，无新增提交能力）

- 空 marker DM1：`result=0 sequence=1`，`pending=0/completed=1`。
- DMA-source TQX copy（GPA→GPU PA 窗口翻译）：`prepared=1`，
  source IOVA 与 GPU PA 分离（`0x104332000`→`0x8904332000`，bias
  `0x8800000000` 一致），一次提交 `verified=1` 双侧匹配。
- 独立 BAR 回读：`verified=Y`，root/directory/leaf present，
  PTE 与预期 GPU PA 一致；`pending=0/completed=2`，无 WARN/Oops。
- helper 模块已各就各位（marker 卸载，tqx 自 pin 留存，readback 用完即卸）；
  主模块引用回到含 retained 实验的稳态。

## RGX 路径勘测（离线，只读）

`mt_live_3d`（r39 实测 106 帧）的提交形状：执行上下文
`node_type=5`（DM2 Universal Queue）+ `submit_context(ctx, command_bo,
{command_va, bytes, type=3→opcode 0x66})`，VM 须经
create→bind→upload→seal。恰好对应 translator T3 缺的三件套，
而 cover plan（r61）正是其输入——链条在概念上已闭合，只差 CCB/命令流内容。

## 排序约束（重要）

本会话已有 retained 且 sealed 的 TQX VM（不可卸载），而 `live_3d` 类实验
要求 `address_spaces/buffers.objects==0` ——**RGX 首次尝试必须在全新会话
（重启后、 retained 实验之前）进行**，不能插队在本会话上。本轮不再加载
任何新实验模块，不再提交。
