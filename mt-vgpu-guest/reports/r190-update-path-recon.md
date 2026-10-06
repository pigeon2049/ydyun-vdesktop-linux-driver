# r190：update 非零 kick 的 DDK2/TA 路径定位 + 活体验证预备（离线）

- **结论**：非零 update 数组不走 `0x88:0x4`（r171 已证伪），而走
  TA 提交链：`RGXKickGfx`/`RGXMultiKickGfx`/`RGXMultiKickUQ` 经
  `SubmissionSetCheckSyncPrim` + `SubmissionSetUpdateSyncPrim`
  编组 check/update 块后，调 `BridgeRGXKickTA3D5` = bridge
  **0x82 function 0x14**（IN 108B / OUT 4B，与 `0x89:0xA` 同形）。
  桥 dispatch 无 `0x82:0x14`（直落 default `-ENOTTY`），requirements
  表亦记 `CMD_LAST`——这就是 STATUS 第二项的 concrete 缺口。
  本轮纯离线 recon（语料按名查询），无代码改动。

## 实测（语料证据；伪 C 为假设，见边界）

1. `SubmissionSetUpdateSyncPrim`（`0015f760`，57 行）：遍历同步表
   （步长 `0x20`），取 `flag&2`（check 为 `flag&1`），上限 32，
   经 `SyncPrimLocalGetHandleAndOffset` 写句柄/偏移/值——与 r159
   三要素逐项吻合（独立 corroborate）。
2. 调用者（calls.jsonl 反查 `0015f760`）：`FUN_0015f890`
   （TQSubmissionSubmit，transfer 链）、`FUN_0016c050`、
   `RGXKickGfx`、`RGXMultiKickGfx`、`RGXMultiKickUQ`。
3. `RGXKickGfx` 内：`SubmissionSetCheckSyncPrim` → ok →
   `SubmissionSetUpdateSyncPrim` → ok → `GetFeatures+0x6c` 选路 →
   `GfxSubmissionSubmit` → `BridgeRGXKickTA3D5`（`FUN_00137c30`）。
4. `FUN_00137c30` 落点：`FUN_00192930(handle,0x82,0x14,&in,0x6c,&out,4)`，
   即 0x82:0x14，IN 108 / OUT 4（与 r151 的 SubmitTransfer3 108/4 同形，
   参数顺序按同族调用一致性解读）。
5. 现状对照：桥 `0x82` 组有 `0xC`（-ENOTTY，S4）、无 `0x14`；
   `requirements.json` 的 `0x82:0x14` 为 `CMD_LAST`（未命名）。

## 活体验证计划（待批：需 bridge 改动 + 重载 + 真实 3D 流量）

1. 桥加 `MT_PVR_FN_RGXKICKTA3D5 0x14U` + accept-and-log observer
   （check/update counts 与数组 digest 只读上报，不执行；仿 r174
   的 `0x89:0xA` 模式）+ 门禁（值钉死 + 路由 + 反向）。
2. 活体：`drm_major=2` 重载 → 真实 3D submision（producer 待定：
   GLES 不通 r119、Rogue2D 死代码 r112、blit 系 TDM 不进 TA——
   producer 本身是 open 问题）→ 观察非零 update → 按 r159 语义
   验证桥写回路径。
3. 在 producer 落定前，update 项仍记“未支持”（STATUS 口径不变）。

## 边界

- 语料伪 C 是路径假设：字段/调用结论未经活体验证；
  `0x6c/4` 的 IN/OUT 解读依赖同族调用形状一致性。
- 语料目录无 SHA 记录文件；corpus 身份沿用 r159/DECOMPILATION.md
  （`b3058c02…`），活体 `.so` 本轮未重验（r184 已验同值）。
- requirements 表的 `CMD_LAST` 本轮不动（生成器产物，等实现轮同步改）。

## 下一步（候选）

- producer recon（谁调 RGXKickGfx：GLES 替代品或 UMD 自带 3D 样例），
  仍可离线；之后按上计划立项。
