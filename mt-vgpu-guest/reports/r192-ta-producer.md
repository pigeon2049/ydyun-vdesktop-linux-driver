# r192：TA producer 路径收敛——harness 可直调导出符号 RGXKickTA（离线）

- **结论**：`RGXKickTA`（+`RGXKickGfx`/`RGXMultiKickGfx`/`RGXMultiKickUQ`）
  均为 UMD 导出符号（`nm -D` 实测），harness 按名直调可行——
  producer 不需要 canned 3D 程序：`RGXKickTA(conn, psKickTA, …)`
  要求 `psKickTA+0x30 != 0`（PrepareTA 产物，即 r85 的墙），
  内走 `RGXPrepareTA(FUN_00178800)` → `RGXSubmitTA(FUN_001796b0)` →
  `0x82:0x14`。`RGXPrepareTA` 仅被此二者调用。本轮纯离线
  （语料按名 + 导出表实测），无代码改动。

## 实测

1. 导出表（`libsrv_um_MUSA.so.1.0.0`，SHA `b3058c02…` 同盘验证）：
   `T RGXKickGfx / RGXKickTA / RGXMultiKickGfx / RGXMultiKickUQ`。
   罐装样例（blit/compute/tq-perf/twiddling）无一进 TA——
   与 r86–r119 一致。
2. `RGXKickTA`（68 行伪 C）：`psKickTA+0x30` 为 PrepareTA 产物指针；
   `+0x4` 为计数/标识。`RGXPrepareTA` 失败即报 `0x864` 返回，
   不碰提交。
3. 调用边（calls.jsonl 反查）：`SubmissionSetUpdateSyncPrim` 的
   调用者含 TQ 链（`FUN_0015f890`）与 Gfx 链（三 Kick*）；
   `RGXPrepareTA` 的调用者仅此二者——update 编组只活在提交链内，
   与 r171（harness 自组包无 update 组装）正交 corroborate。
4. 语料 SHA 说明：语料目录无 SHA 记录文件；身份沿用
   r159/DECOMPILATION.md，活体 `.so` 为 r184 同值。

## 活体 ladder 计划（待批）

1. 离线先行：psKickTA 构造（哪个 UMD API 铸造它）+ PrepareTA
   输入整形（续 r85）——仍可零硬件触碰。
2. 活体（批准后）：harness rung 链 `… → RGXKickTA`，
   目标 `0x82:0x14` 首个真实 update 非零包；桥侧先行
   accept-and-log observer（r190 计划）接包。
3. 在此之前 update 项仍记“未支持”。

## 边界

- 伪 C 为路径假设；`+0x30/+0x4` 语义待整形实测。
- 本轮无代码改动，门禁数不变（295+292）。

## 下一步（候选）

- psKickTA 构造 recon（离线）；之后按上计划立项。
