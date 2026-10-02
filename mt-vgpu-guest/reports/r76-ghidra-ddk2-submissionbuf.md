# r76：ghidra 语料指路——DDK2 `+0x28` 是 SubmissionBuf 分配器 + CCB pack 公式

用户指的两处（`/opt/MTT-driver-only/` 官方驱动包 + `ghidra-projects/` 可重开
工程）砍对了地方：`decompiled/linux-legacy-umd-5.2.0/` 的伪 C 与 SHA
（`b3058c02…`，与在用 UMD 一致）让 DDK2 入参问题从"反汇编硬啃"变成
"查语料 + 3 次重放验证"。结论：DDK2 缺的 rsi 对象在 create 阶段由
`SubmissionBufAlloctorCreate` 填入（`+0x28`，`+0x48` 置零——正是崩溃点形状）；
CCB pack 公式 trace 实测命中。全程离线 + fabricated，零硬件触碰。

## 语料用法（以后查 UMD 先走这里，不重起 Ghidra）

`functions.jsonl` 按名取地址 + `decompiled.c` 行号（`c_line_start`），
`RGXKickSyncDDK2@0x52fc0`（325 行伪 C）、`RGXCreateKickSyncContextCCB@0x52180`
等与 objdump 地址一一对应。教训：r74/r75 用 objdump 走 corrupt 的两处
（`0xd0` 误归因、"第二循环"误判）伪 C 一眼即正——以后 RE 结论必须过一遍语料。

## `+0x28` 身份（`RGXCreateKickSyncContextCCB` 伪 C）

```c
plVar3 = PVRSRVAllocUserModeMem(0x30);   /* +0x28 缺省为零 */
// ... features>=2 且 SyncPrim 两步成功才走：
plVar3[4] = 0;                            /* +0x20 计数器清零 */
SubmissionBufAlloctorCreate(*plVar3, plVar3[3], plVar3 + 5);  /* +0x28 填入 */
*(plVar3[5] + 0x48) = 0;                  /* 出生即 +0x48 置零 */
```

门控链：`GetFeatures+0x54 >= 2` → `SyncPrimAllocWitchMemType` →
`SyncPrimSet` → `SubmissionBufAlloctorCreate`。任一失败回 legacy
桥（`FUN_00135ce0` 即 `0x88:0x0`），`+0x28` 保持 NULL。

## CCB pack 公式（fabricated 实测 3/3）

`call RGXCreateKickSyncContextCCB b7* b5 u0 u0x33 u0x07 u0 b12` →
`0x88:0x0 IN = …33070000`，即：

```text
ui32PackedCCBSizeU88 = (create_arg5 & 0xff) << 8 | (create_arg4 & 0xff)
```

r56"合成路径 CCB size 为 0"的精确机制：rung6 四个 arg 全零 →
pack 结果恒零 → server 侧不持有 CCB。

## 离线世界的边界（重要）

ccb trace 共 58 条桥记录，以 `0x88:0x0` 收尾、其后无 SyncPrim
分配——fabricated 环境恒走 legacy 分支，`+0x28` 永为 NULL。
**fabricated DDK2 原则上不可驱动**；下一步必须是活体 passthrough：
非零 CCB create → SyncPrim 真分配 → `+0x28` 存活 → DDK2。
那是一次 live 实验，需单独批准，本轮未碰。
