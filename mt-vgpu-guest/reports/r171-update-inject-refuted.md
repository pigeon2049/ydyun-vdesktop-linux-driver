# r171：update 活体注入被证伪——legacy RGXKickSync 自组 wire 包，harness 递不进 update 数组（批准执行）

- **结论**：手写 rung9（wire IN 摆 `update_count=1` + 三数组）经 GDB 监督跑出 `RGXKickSync → 1` 且**无 `0x88:0x4` 桥调用**；fence 名、PMR 柄两变体同败。语料 `RGXKickSync`（`decompiled.c:28424`）显示：harness 递的 b26 是 UMD CMD 对象（check count 在 `+0xD8`、数组在 `+0xE0` 起），84B wire 布局与之错位（读越界，堆垃圾致 `FUN_00177920` 失败返回）；且该函数内**无 update 数组组装**——wire 包的 update 欄位由 UMD 内部另行填充（r74 结论被独立 corroborate）。故 legacy 路径下 update 活体注入此路不通；活体验证需 DDK2 提交链（`SubmissionSetUpdateSyncPrim` 显式编组，r159），即 bridge 以 `drm_major=2` 重载——被 freeze 禁止，单独立项待批。会话健康，freeze 继续。

## 实测

1. rung9 配方：SYNC 前缀 + kicksync-create + `buf26 84B`（ctx@0、三数组指针@36/44/52、count@60=1）+ `RGXKickSync`。standalone SIGSEGV（老 curse），GDB 监督下 `→ 1`，trace 126 行**零 `0x88:0x4`**。证据存 [`r171-rung9-ret1.jsonl`](r171-rung9-ret1.jsonl)。
2. 两变体同败：`update_fence_name` 置字符串；`update_ufo` 改 PMR 柄（`'b11+8'`，顺带验证 harness 的 `bID+OFF` 解引用可用）。排除此二为单因。
3. UMD 侧无日志（`PVRSRVDebugPrintf` 断点仅见 connect 期 HWPerf 噪声），失败静默——与“入参校验未过直接返错”一致。
4. 语料关键行：`param_3[0x36]`（check count）、`param_3+0x38/0x3c…` 数组、`FUN_00177920` 组装、`param_3[0x6c]` 后续；`return 1` 来自组装失败（`iVar11` 原样返回），非桥拒绝（桥未被调用）。
5. 会话健康：probe ref 1，bridge ref 0，`translate_kick` 仍 off，无 GPU 提交，dmesg 干净。

## 边界

- “84B 错位读越界”有堆越界读风险（只读 216B 处的垃圾 count，未写）——harness 进程行为，无内核影响；该配方不再复用。
- DDK2 活体链仍被 freeze 挡住；本轮未动桥参数、未重载。

## 下一步（候选，需批准）

- 桥以 `drm_major=2` 重载（rmmod+insmod，打破当前 freeze）→ DDK2 全链 + update 非零 kick 活体验证；或维持 freeze，CCB/ translator 走离线。
