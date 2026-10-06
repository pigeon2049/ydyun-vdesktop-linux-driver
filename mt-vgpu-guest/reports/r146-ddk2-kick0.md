# r146：DDK2 kick 语义——零 count 同步 kick 仍走 `0x88:0x4`，原样受理（批准执行）

- **结论**：`drm_major=2` 下，DDK2 CCB 上的零 count `RGXKickSync` 走的仍是
  legacy `0x88:0x4`（84B IN），桥 accept-and-inspect 原样受理（ret 0，即时 fence），
  全链六符号全 0、harness exit 0、轨迹零非零。
  DDK2 专属提交口是另两条（TA 用 `0x82:0xC`、CDM 用 `0x81:0x5`，均未实现，
  属 S4 真提交边界，本轮不碰）。
- 零 timeout；除 accept-and-inspect 的即时 fence 外未提交 GPU 工作；probe 未动；
  桥恢复默认 freeze（ref 1/0，L3 全绿）。

## 实测（执行过）

1. 新桥（`=2`，读回 2）：rung8 形（rung5 + 零 count CCB + `RGXKickSync` 224B 包，
   `b5*` 修正）→ 六符号全 0，exit 0。证据：`reports/r146-major2-kick0.jsonl`。
   提交相关调用：`0x82:0x12`(12) → `0x88:0x5`(8) → **`0x88:0x4`(84)**，
   无 `0x82:0xC`/`0x81:0x5`。
2. 语料对照（decompiled.c）：`BridgeRGXKickSync2`=`0x88:0x2`（56B），
   `BridgeRGXKickTA3D2`=`0x82:0xC`（IN 268B/OUT 12B），
   `BridgeRGXKickCDM2`=`0x81:0x5`（IN 108B，与注释中"故意不实现"一致）。
   零 count 同步 kick 不在其列——与活体一致。
3. 恢复：桥默认（0/0），node probe 0 failing，ref 1/0，dmesg 零 WARNING。

## 下一步（Translator T3 主线，STATUS 第 2 项）

- 真实绘制的非零 CCB 内容仍是缺失输入：Rogue2D 已到 TransferContext 创建；
  r113 check-only 首帧翻译设计待新会话执行——这是在 DDK2 路径上拿真实
  CCB 内容的第一条路（UMD 现已全程走 DDK2，legacy-TDM 死代码判断更实）。
- 遗留：81+ 提交未 push；`r135` jsonl 未动。
