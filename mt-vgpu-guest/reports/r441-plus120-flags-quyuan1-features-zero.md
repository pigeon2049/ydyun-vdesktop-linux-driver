# r441: +0x120 flags 完整位表 + QuYuan1 feature 字段恒零（离线反汇编）

> Round r441 (2026-10-09). **纯离线，零硬件触碰。** r440（tile 打包活体仍超时）后，
> Header vs UMD 单 RT 必写清单仅剩 `+0x120`（flags 位打包）与 `+0x138`–`+0x160`
> （feature 条件）未填。本轮逐行提取两者语义。

## Conclusion

**三个结论（[MEASURED] 反汇编，`linux-legacy-umd-5.2.0/decompiled.c`）：**

1. **`+0x120` 完整 11-bit 打包表已提取**（`FUN_00178800`=RGXPrepareTA，
   :52118 初始化 0，:52194–52234 逐 bit OR 入）。除 bit0（UMD 内部
   RTDataSet 状态）外，其余 10 bit 全部源自 DDK 层设置的 psKickTA flags
   （`*param_2`），UMD 语料内无 DDK 构造点，取值 [UNKNOWN]。
2. **Feature 字段在目标机（QuYuan1/S3000）上恒零**——官方源码把 S3000
   PCI ID `0x0222` 映射到 `quyuan1_drvdata`；QuYuan1 feature 表
   byte+2=0x43（bit2=0）使写入条件恒为假，UMD 只显式清零 `+0x140`/`+0x150`，
   其余不写。**我方 Header-only 全零与 UMD 行为一致** ✅ —— r438 P3 嫌疑关闭。
3. **纠正调用点误读**：反编译显示 `lVar6 = GetFeatures();`（无参），但
   objdump（0x78850）证实调用前 RDI=param_1 未被改写，实际为
   `GetFeatures(param_1)`，返回 `*(param_1+0xa0)+0x620`。无参恒零假设不成立。

## 1. +0x120 位打包表 [MEASURED]

### 写入点

- :52118 `*(undefined4 *)(lVar8 + 0x120) = 0;`（初始化，LAB_00178a50 内，
  先于 feature 条件块）
- :52194–52234 逐 bit 打包（`lVar8`=TA_buf，`param_2`=psKickTA）

### 完整位表

| Bit | 置位条件 | 来源 | 行 |
|---|---|---|---|
| 0 | `(RTDataSet+0x00 & 2)==0` 时置 1 | UMD 内部 | 52194–52197 |
| 1 | psKickTA.flags bit0 或 bit3（`uVar5=*param_2; (uVar5&9)!=0`） | DDK | 52212–52215 |
| 4 | psKickTA.flags bit19（`& 0x80000`） | DDK | 52209–52211 |
| 8 | psKickTA.flags bit3（`(&8)<<5`） | DDK | 52201 |
| 9 | psKickTA.flags bit12（`>>3 & 0x200`） | DDK | 52203 |
| 10 | psKickTA.flags bit13（`>>3 & 0x400`） | DDK | 52205 |
| 13 | psKickTA.flags bit17（`>>4 & 0x2000`） | DDK | 52207 |
| 20 | psKickTA.flags bit24（`>>4 & 0x100000`） | DDK | 52216 |
| 21 | `RTDataSet+0x20!=0` 且 `RTDataSet+0x20==psKickTA+8` 时置 1（否则清零） | 混合 | 52222–52227 |
| 22 | `RTDataSet+0x20!=0` 且 `psKickTA+8==RTDataSet+0x24` 时置 1（否则清零） | 混合 | 52228–52233 |
| 23 | psKickTA.flags bit25（`>>2 & 0x800000`） | DDK | 52218 |

变量身份 [MEASURED]（:52055–52076）：
- `puVar2 = *(uint **)(param_2 + 0xc)`，`lVar3 = *(long *)(param_2 + 0xc)` ——
  均为 RTDataSet（psKickTA+0x0c）
- `local_40 = *puVar2; local_40 &= 2;` —— RTDataSet+0x00 的 bit1
- `uVar5 = *param_2`（:52110）—— psKickTA flags dword

### 门控前置（early-out）[MEASURED]（:52107–52116）

```c
uVar5 = *param_2;
if ((uVar5 & 0x20) == 0) {
  if ((uVar5 & 0x10) == 0) goto LAB_00178a50;  // bit4,bit5 均清 → 写 TA_buf
} else {
  if ((uVar5 & 0x10) != 0) {
    FUN_00178320(param_1,param_2,lVar11,puVar2,lVar3,0);
    goto LAB_00178908;  // bit4+bit5 全置 → 跳过 TA_buf 写入
  }
}
```

### 典型值分析

- **bit0**：RTDataSet 经 `PVRSRVCallocUserModeMem` 分配（零初始化），
  `SetupRTDataSet`/`SetupRTDataSetMemSize` 均未写 `+0x00`
  （:48850–48890，:50151–50170），故 `RTDataSet+0x00` 通常为 0 →
  bit0 通常为 **1** [INFERRED 高置信]。
- **其余 10 bit**：依赖 DDK flags（`*param_2`）与 RTDataSet+0x20/+0x24，
  UMD 语料内无 DDK 构造点，取值 [UNKNOWN]。
- **UMD 忠实最小值**：DDK flags 全零且 RTDataSet+0x20=0 时，
  `+0x120 = 0x1`（仅 bit0）。我方当前写 0，与此差 bit0。

## 2. Feature 字段 `+0x138`–`+0x160` [MEASURED]

### 条件与写入（:52120–52134）

```c
lVar6 = GetFeatures();  // 实际传参见 §3
if (((lVar6 == 0) || ((*(byte *)(lVar6 + 2) & 4) == 0))
    || (*(int *)(lVar6 + 0x10) == 0)) {
  // FALSE 分支：
  *(undefined4 *)(lVar11 + 0x208) = 0;
  *(undefined4 *)(lVar8 + 0x140) = 0;
  *(undefined4 *)(lVar8 + 0x150) = 0;
  // +0x138/+0x148/+0x158/+0x160 不写入（零初始化缓冲中保持 0）
} else {
  // TRUE 分支：
  *(int *)(lVar8 + 0x138) = *(int *)(lVar6 + 0x10);
  *(uint *)(lVar8 + 0x140) = puVar2[0x171];
  *(undefined4 *)(lVar8 + 0x148) = *(undefined4 *)(lVar6 + 0x18);
  *(undefined4 *)(lVar8 + 0x150) = *(undefined4 *)(lVar6 + 0x1c);
  *(undefined4 *)(lVar8 + 0x158) = 0;
  *(undefined4 *)(lVar8 + 0x160) = 0;
}
```

### 四芯片对照（.data 实测，objdump）

| 芯片 | 地址 | byte+2 | bit2 | dword+0x10 | 分支 | +0x138/+0x148/+0x150 |
|---|---|---|---|---|---|---|
| Sudi | 0xb575e0 | 0x40 | 0 | — | FALSE | 0/0/0 |
| **QuYuan1** | 0xb57560 | 0x43 | 0 | — | **FALSE** | **0/0/0** |
| QuYuan2 | 0xb574e0 | 0xff | 1 | 1 | TRUE | 1/0/4 |
| PingHu1 | 0xb57460 | 0xff | 1 | 0 | FALSE | 0/0/0 |

### 目标机判定

官方 Linux 源码（5.1.0/5.2.0）把 S3000 PCI ID `0x0222` 映射到
`quyuan1_drvdata`（reports/official-linux-guest-port.md [MEASURED]）。
目标机（yuyun 00:0e.0 `1ed5:0222`）= **QuYuan1** → **FALSE 分支** →
`+0x138`–`+0x160` 在真实 UMD 提交中恒为 0。

**结论：我方 Header-only（全零）与 QuYuan1 UMD 行为一致，feature 字段
不是 mismatch。r438 P3 嫌疑关闭。**

## 3. GetFeatures 调用点纠正 [MEASURED]

- 反编译 :52076 `lVar6 = GetFeatures();` 显示无参。
- objdump 0x78850：`call 518e0 <GetFeatures>` 前 `mov %rdi,%r14` 仅保存，
  RDI（=RGXPrepareTA 的 param_1）未被改写 → **实际调用 `GetFeatures(param_1)`**。
- `GetFeatures`（:27851）：`*(param_1+0xa0)+0x620`（有效时），否则 0 并打印
  "Input Param is invalid."。
- 另有 :52093 `lVar7 = GetFeatures(param_1);`（显式传参）用于
  `*(lVar7+0x54)<2` 的 RTData 布局选择，与 lVar6 为两次独立调用。

## 4. r442 前置

1. **P0（实现）**：`mt_ta_real_buffer_build()` 中 `+0x120` 写 `0x1`
   （bit0，UMD 忠实最小值 [INFERRED 高置信]）；同步 T5 白名单
   （`+0x120` 加入允许集合）与测试。
2. **P1（仍 UNKNOWN）**：`+0x68` 布尔（r438，DDK 取值未知）。
3. **已关闭**：feature 字段（本轮，QuYuan1 恒零，与我方一致）。
4. DDK flags（psKickTA `*param_2`）的真实取值仍 [UNKNOWN]；
   若 P0 无效，需另寻 DDK 行为依据（无 UMD 捕获环境，不盲试）。

## 5. Honest boundaries

- `+0x120` 各 bit 的打包逻辑 [MEASURED]；DDK flags 取值 [UNKNOWN]；
  bit0=1 为 [INFERRED 高置信]（calloc + 无 +0x00 写入点）。
- Feature 表芯片值 [.data 实测]；S3000→QuYuan1 映射 [MEASURED 官方源码]。
- `puVar2[0x171]`（QuYuan2 TRUE 分支的 `+0x140` 来源）语义未深究——
  目标机用不到。
- **本轮零硬件触碰，纯离线，无代码变更。**
