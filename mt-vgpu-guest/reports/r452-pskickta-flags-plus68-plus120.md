# r452: DDK psKickTA flags 深度分析——`+0x68` 语义与 `+0x120` 逐位含义（离线）

> Round r452 (2026-10-09). **纯离线，零硬件触碰。** r451（`+0x120=0x1` 活体仍 5s 超时）
> 后，Header vs UMD 单 RT 必写清单仅剩 `+0x68`（r438 P1，唯一主要 UNKNOWN）
> 与 `+0x120` 其余十位未解决。本轮通过反汇编实测 + 架构推理，确定两者语义。

## Conclusion

**四个结论：**

1. **`+0x68` = `((psKickTA.flags & 3) == 3)` [MEASURED]**——bit0 与 bit1 全置才写 1。
   DDK 位定义 [UNKNOWN]（专有 DDK，UMD 语料无构造点）。基于使用模式
   [INFERRED]：bit0/bit1 很可能为 TA kick 的"首/尾"或"TA/3D"配对标志；
   **单 RT 纯 TA kick 下 UMD 真实值很可能为 0**（我方当前 0 可能正确）。
2. **`+0x120` 11-bit 完整语义表已建立**（见 §2）。除 bit0（UMD 内部，
   [INFERRED 高置信] 通常 1）外，其余 10 bit 全部源自 DDK psKickTA.flags，
   具体位映射 [MEASURED]，DDK 取值 [UNKNOWN]。
3. **DDK flags 在 UMD 内的完整使用模式 [MEASURED]**（RGXPrepareTA，
   `FUN_00178800`）：共 12 个不同 bit 被使用（0,1,3,4,5,11,12,13,17,19,24,25），
   用途分为四类：`+0x68` 布尔、`+0x120` 打包、early-out 控制、3D 命令构造。
4. **不建议盲试 `+0x68=1`**——无证据支持单 RT TA kick 需要 bit0+bit1 全置；
   若 DDK 语义为"TA+3D 双 kick"，我方纯 TA 场景写 1 反而是 mismatch。
   **超时根因仍未知，需继续离线深挖或等待固件行为实测。**

## 1. `+0x68` 深度分析

### 1.1 写入点 [MEASURED]

`decompiled.c`（`FUN_00178800` = `RGXPrepareTA`）：

| 行号 | 代码 | 路径 |
|---|---|---|
| 52136 | `*(uint *)(lVar8 + 0x68) = (uint)((*param_2 & 3) == 3);` | 主 TA_buf |
| 52357 | `*(uint *)(lVar4 + 0x68) = (uint)((*param_2 & 3) == 3);` | 第二 TA_buf |

两处逐字相同。`param_2` = psKickTA（`RGXKickTA`/`RGXKickGfx` 透传，
r438 已证实），`*param_2` 为其首 dword = flags。

**语义**：`+0x68` 为 4B 布尔，**当且仅当 flags bit0 与 bit1 全置时写 1，
否则写 0**。

### 1.2 DDK flags 位定义 [UNKNOWN]

psKickTA 由 DDK 层（client）分配初始化，经 `RGXKickTA`（导出 API）
传入 UMD。UMD 反汇编语料内无 psKickTA 构造点（`RGXKickTA` 仅透传，
`RGXKickGfx` 同理）。官方 vGPU 源码（`official-vgpu-2.3.0`）与 MTGPU
源码（`mtgpu-2.7.1-6.12`）均为内核侧，无 DDK userspace 头文件。

**DDK 的 `RGX_KICKTA_FLAGS_*` 常量定义不在任何可用源码中。**
以下为基于使用模式的推理，证据等级明确标注。

### 1.3 使用模式分析 [MEASURED]

RGXPrepareTA 内 `*param_2`（flags）的全部使用：

| Bit | 掩码 | 用途 | 行号 |
|---|---|---|---|
| 0+1 | `& 3` | `+0x68 = ((flags & 3) == 3)` | 52136, 52357 |
| 0 或 3 | `& 9` | `+0x120` bit1 置位 | 52212–52215 |
| 3 | `& 8` | `+0x120` bit8 (`<<5`) | 52201 |
| 4+5 | `& 0x30` | early-out 控制（见 §1.4） | 52107–52116 |
| 11 | `>> 0xb` | `FUN_00181420` 参数 (`(^1)&1`) | 51891 |
| 12 | `>> 3 & 0x200` | `+0x120` bit9 | 52203 |
| 13 | `>> 3 & 0x400` | `+0x120` bit10 | 52205 |
| 17 | `>> 4 & 0x2000` | `+0x120` bit13 | 52207 |
| 19 | `& 0x80000` | `+0x120` bit4 | 52209–52211 |
| 24 | `>> 4 & 0x100000` | `+0x120` bit20 | 52216 |
| 25 | `>> 2 & 0x800000` | `+0x120` bit23 | 52218 |
| 11 | `& 0x800` | `FUN_00178320` 内命令字选择 | 51897 |

### 1.4 Early-out 语义 [MEASURED + INFERRED]

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

**[MEASURED]**：bit4+bit5 全置时，RGXPrepareTA **跳过** 360B TA_buf
Header 写入，直接调用 `FUN_00178320`（固件命令构造）。

**[INFERRED 中置信]**：bit4/bit5 很可能为"无 TA 工作"或"3D-only"
标志。当 DDK 确定本次 kick 无几何数据需 TA 处理时，置位跳过
Header 准备。我方场景（有几何数据，需 TA）应保持 bit4=bit5=0，
与当前实现（flags 全零）一致。

### 1.5 `+0x68` 语义推理 [INFERRED 低置信]

**已知**：
- `+0x68=1` 要求 `flags.bit0=1 AND flags.bit1=1`
- `+0x120` bit1 要求 `flags.bit0=1 OR flags.bit3=1`

**PowerVR Rogue 架构背景**（公开知识）：
- TA（Tile Accelerator）处理几何、分块
- 3D（ISP）处理光栅化
- Kick 可为纯 TA、纯 3D、或 TA+3D

**候选语义**（按可能性排序）：

1. **"TA+3D 双 kick"指示** [INFERRED 低置信]：
   - bit0 = "含 TA 工作"，bit1 = "含 3D 工作"
   - `+0x68=1` 表示本次 kick 同时提交 TA 与 3D
   - 我方 `mt-ta-readback` 为纯 TA 测试（无 3D 命令），DDK 若按此语义，
     真实值应为 0（仅 bit0 置位）
   - **我方当前写 0 可能正确**

2. **"首/尾 kick"配对** [INFERRED 低置信]：
   - bit0 = "首 kick"，bit1 = "尾 kick"
   - `+0x68=1` 表示单次 kick 即完整帧（首尾合一）
   - 无法从 UMD 使用模式证实或证伪

3. **"同步/栅栏"相关** [INFERRED 极低置信]：
   - 与 TA fence 同步行为相关
   - 无直接证据

**诚实边界**：以上均为推理，DDK 真实位定义 [UNKNOWN]。
**无证据支持在单 RT 纯 TA 场景下将 `+0x68` 置 1。**

### 1.6 对我方实现的意义

| 场景 | DDK flags bit0/bit1 | UMD 写 `+0x68` | 我方写 0 |
|---|---|---|---|
| 纯 TA kick（推测） | bit0=1, bit1=0 | 0 | ✅ 一致 |
| TA+3D 双 kick（推测） | bit0=1, bit1=1 | 1 | ❌ mismatch |
| 无 TA（early-out） | bit4=bit5=1 | 不写 Header | N/A |

**若语义 (1) 成立，我方纯 TA 场景写 0 正确，`+0x68` 不是超时根因。**

## 2. `+0x120` 完整 11-bit 语义表

### 2.1 位映射 [MEASURED]（r441 已提取，本轮复核）

| Bit | 置位条件 | DDK flags 源 | 证据等级 |
|---|---|---|---|
| 0 | `(RTDataSet+0x00 & 2)==0` | UMD 内部 | [MEASURED] 表达式；[INFERRED 高置信] 通常 1 |
| 1 | `(flags & 9)!=0` | bit0 或 bit3 | [MEASURED] |
| 4 | `(flags & 0x80000)!=0` | bit19 | [MEASURED] |
| 8 | `(flags & 8)<<5` | bit3 | [MEASURED] |
| 9 | `(flags>>3 & 0x200)` | bit12 | [MEASURED] |
| 10 | `(flags>>3 & 0x400)` | bit13 | [MEASURED] |
| 13 | `(flags>>4 & 0x2000)` | bit17 | [MEASURED] |
| 20 | `(flags>>4 & 0x100000)` | bit24 | [MEASURED] |
| 21 | `RTDataSet+0x20!=0 && ==psKickTA+8` | 混合 | [MEASURED] |
| 22 | `RTDataSet+0x20!=0 && psKickTA+8==RTDataSet+0x24` | 混合 | [MEASURED] |
| 23 | `(flags>>2 & 0x800000)` | bit25 | [MEASURED] |

### 2.2 DDK 源 bit 的使用上下文 [MEASURED]

**bit0/bit3（→ `+0x120` bit1/bit8）**：
- bit0 同时参与 `+0x68`（`&3`）与 `+0x120` bit1（`&9`）
- bit3 同时参与 `+0x120` bit1（`&9`）与 bit8（`<<5`）
- [INFERRED]：bit0/bit3 很可能为 TA kick 的基础使能标志

**bit12/bit13（→ `+0x120` bit9/bit10）**：
- 相邻 bit，相邻目标位
- [INFERRED 低置信]：可能为 MSAA 或渲染目标属性

**bit17/bit19（→ `+0x120` bit13/bit4）**：
- 非相邻，但均为高位
- [UNKNOWN] 语义

**bit24/bit25（→ `+0x120` bit20/bit23）**：
- 最高使用的 bit
- [UNKNOWN] 语义

### 2.3 单 RT 最小场景的 UMD 真实值 [INFERRED]

**假设**：DDK flags 全零（最简 TA kick），RTDataSet 零初始化，
RTDataSet+0x20=0：

| Bit | 值 | 依据 |
|---|---|---|
| 0 | 1 | [INFERRED 高置信] RTDataSet calloc 零初始化 |
| 1,4,8,9,10,13,20,23 | 0 | DDK flags 全零 [假设] |
| 21,22 | 0 | RTDataSet+0x20=0 [假设] |

**UMD 忠实最小值 = `0x1`**（r441/r442 已实现，r451 活体验证未解决超时）。

**若 DDK 在真实场景置位任一 flag bit，`+0x120` 真实值 > `0x1`。**
具体哪些 bit 会被置位 [UNKNOWN]——需 DDK 行为实测或头文件。

## 3. 证据等级总表

| 项目 | 结论 | 等级 |
|---|---|---|
| `+0x68` 表达式 `((flags&3)==3)` | 写 1 条件为 bit0+bit1 全置 | [MEASURED] |
| `+0x68` DDK 位定义 | bit0/bit1 真实语义 | [UNKNOWN] |
| `+0x68` 单 RT TA 真实值 | 很可能为 0（若语义为 TA+3D） | [INFERRED 低置信] |
| `+0x120` 11-bit 映射 | DDK 源 bit 位置 | [MEASURED] |
| `+0x120` bit0=1 | RTDataSet 零初始化 | [INFERRED 高置信] |
| `+0x120` 其余十位 DDK 取值 | 真实场景的置位情况 | [UNKNOWN] |
| `+0x120=0x1` 充分性 | 是否满足固件 | [UNCONFIRMED]（r451 超时未证伪也未证实） |
| Early-out bit4+bit5 | 跳过 Header 写入 | [MEASURED] |
| Early-out 语义 | "无 TA 工作" | [INFERRED 中置信] |

## 4. 下一步建议

1. **不盲试 `+0x68=1`**——无证据支持；若语义为 TA+3D，纯 TA 写 1 是 mismatch。
2. **P1**：若有可能，获取 DDK 头文件或真实 DDK 的 psKickTA.flags 取值
   （如通过 pvrdebug 或 DDK 日志）。
3. **P2**：考虑固件超时根因可能不在 Header——r451 已证实 13 BO 绑定、
   render context READY，但固件无响应。可能需检查：
   - TA 命令流（`pui8TACmd`）内容是否合法
   - 固件是否需要 3D kick 配合（即使纯 TA 测试）
   - Bridge 层参数（如 `ui32TACmdSize`）是否正确

## 5. 门禁

- `make -C mt-vgpu-guest check-offline`：**589 Python + 781 C 全绿**
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 本轮纯离线分析，无生产代码变更

---

**诚实边界**：本报告所有 [INFERRED] 均基于反汇编使用模式与 PowerVR
公开架构知识，DDK 专有位定义 [UNKNOWN]。未做任何活体验证。
