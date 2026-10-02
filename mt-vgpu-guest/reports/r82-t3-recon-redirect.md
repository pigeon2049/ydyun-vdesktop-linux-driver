# r82：T3 recon 第一锹——mtkm64 侧无果有因，转向 UMD 侧 TA 组装

本轮开 T3（DM 格式）主线，快速试错结论：`mtkm64.sys` 语料库不是
T3 输入的正确来源（它是 host 侧物理 GPU KMD，讲 RGX_CR 寄存器，
不讲 guest kick 环）；真正的 T3 输入在 Linux UMD 语料里——
`PrepareTA（FUN_00178800@0x178800，218 行伪 C）`把 TA kick 的
控制字组装过程摊开了，且再次命中 `features+0x54` 门控。
全程离线语料查询，零硬件触碰。

## mtkm64 侧（排除项，记录以免后人重踩）

- `functions.jsonl`（4344 项）无 Kick/Submit/Ring/DM/TA 本地名；
  `decompiled.c` 零 `KickSync` 引用——它是 host KMD，没有 guest
  桥语义（`0x88` 系列在其世界不存在）。
- 仅有的 RGX 痕迹是 `RGX_CR_CDM_*` 调试串（context store/load/
  terminate），指向物理寄存器编程——与我方固件通道（TQX/DM marker +
  页表，probe 已打通）无关。T3 不从这里反推（STATUS 原句"RGX 环待从
  mtkm64.sys 反推"就此修正：host 环 ≠ guest 提交格式）。

## UMD 侧（T3 正源，首图）

`PrepareTA`（`decompiled/linux-legacy-umd-5.2.0/decompiled.c L52052`）：
- 输入：kick 客户端结构（`param_2`，`+0xc` 缓冲指针、`+0xb6/0xb8/0xba`
  三个 u64、`+0x1c8…` 输出槽）；输出：`param_3` TA 提交记录
 （`[0..2]` 三 u64 + `[3]=param_2` 回指）。
- 控制字落点：`lVar8+0x120/0x140/0x150`、`lVar11+0x208`（TA 状态缓冲，
  条件清零）——DM 化前要逐字解的正是这类缓冲。
- **同一门控第三次出现**：`features+0x54 < 2` 走 legacy 基址 `+0x38`，
  否则 `+0x228`（步长同为 `0xd0`）。我方桥钉死 legacy（r78），故 UMD
  的 TA 组装恒走 `+0x38` 分支——T3 只需解 legacy 版（范围减半，利好）。

## 下一步（T3 第二锹）

沿 `PrepareTA` 的调用者（`RGXKickTA@0x7afd0`）向上、被调
（`FUN_00178320` 等）向下，把"TA 提交记录 → DM2 包"的完整变换链
画出来，再对照 `live_3d_drm` 已验证的 DM2 信封看哪些字段是现成的、
哪些要翻译。这仍是离线工作，不需要批准。
