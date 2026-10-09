# r408：RGXSubmitTA 不构造 TA 缓冲——360B 内容由客户端生成，r409 需另寻 TA 指令格式

> 轮次：r408（2026-10-09）。**纯离线反汇编，零硬件触碰。**
> 背景：r405 路线图第三步；r407 验证捕获机制但未得真实负载；本轮解析 UMD `RGXSubmitTA` 的 TA 缓冲构造逻辑。

## 结论

**`RGXSubmitTA` 不构造 360B TA 命令缓冲——它只透传客户端提供的 device VA。** 缓冲内容（TA 指令流）由 UMD 客户端（3D 应用/状态机）在调用 `RGXKickTA` 之前生成，`RGXSubmitTA` 内无任何填充、模板或构造逻辑。r409 的"最小真实 TA 包"不能从 `RGXSubmitTA` 反汇编中直接得到；需另寻 TA 指令编码（固件文档、KMD 源码或真实应用 trace）。

## 反汇编证据

语料：`decompiled/linux-legacy-umd-5.2.0`（MUSA DDK 5.2.0 UMD）。

### 调用链（已确认）

```
RGXKickTA (0x17afd0, 68 行)
  → RGXPrepareTA (FUN_00178800, 218 行)
  → RGXSubmitTA (FUN_001796b0, 709 行)
      → BridgeRGXKickTA3D3 (FUN_00137180, 168 行)
          → SubmitTADataEnQueue
```

`RGXKickTA` 伪 C（`decompiled.c:53180`）：
- 入口检查 `psKickTA+0x30 != 0`（PrepareTA 产物），否则报 `0x85a`
- 依次调用 `FUN_00178800`（PrepareTA）；成功后调用 `FUN_001796b0`（SubmitTA）
- 失败分别报 `"RGXPrepareTA failed"` / `"RGXSubmitTA failed"`

### RGXSubmitTA 不构造缓冲（已确认）

`FUN_001796b0`（`decompiled.c:52471`，709 行）内：

```c
lVar23 = *param_3;          // TA 缓冲 device VA，直接取自 psKickTA[0]
lVar3  = param_3[1];
lVar4  = param_3[2];        // 另一缓冲 VA
...
iVar9 = FUN_00137180(..., &local_548, local_4d0,
                     -(uint)(lVar23 != 0) & 0x168, lVar23,   // size=0x168=360, VA=lVar23
                     -(uint)(lVar4 != 0) & 0x220, lVar4,     // size=0x220=544
                     0x220, lVar3, ...);
```

- `0x168`（360）是以**立即数字面量**出现的尺寸参数，`lVar23` 是传入的 VA——函数内无任何对该缓冲的写入、memset、memcpy 或模板填充
- 全函数 709 行中无 `0x66` TA opcode 写入、无 360B 相关的构造循环
- 三个变体（`FUN_00137180` / `FUN_00136ec0` / `FUN_00137750`）均为 bridge 打包，差异仅在同步原语路径

### RGXPrepareTA 只回填指针（已确认）

`FUN_00178800`（`decompiled.c:52052`，218 行）：

```c
*param_3   = *(long *)(param_2 + 0xb6);   // psKickTA[0] = render_ctx+0xb6 (TA VA)
param_3[1] = *(long *)(param_2 + 0xb8);
param_3[2] = *(long *)(param_2 + 0xba);   // psKickTA[2] (544B VA)
param_3[3] = (long)param_2;               // psKickTA[3] = render context 指针
```

- 只做指针搬运 + `+0x1c8–0x1d4` 状态回填；不分配、不构造缓冲内容
- 与 r193 结论一致："无铸造函数，harness 须手塑"

### psKickTA 布局（已确认 / 推断）

| 偏移 | 字段 | 来源 | 状态 |
|---|---|---|---|
| +0x00 | TA 缓冲 device VA（360B） | render_ctx+0xb6（PrepareTA 回填） | 已确认 |
| +0x08 | 未知（lVar3，配 0x220 尺寸参数） | render_ctx+0xb8 | 已确认存在，语义未知 |
| +0x10 | 缓冲 device VA（544B = 0x220） | render_ctx+0xba | 已确认存在，语义未知 |
| +0x18 | render context 指针 | PrepareTA 回填 param_2 | 已确认 |
| +0x28 | 同步相关（param_3[5] 传入 fence 等待） | 调用方 | 推断 |
| +0x30 | PrepareTA 产物指针（非空检查） | PrepareTA | 已确认（r192） |

### 三层布局对照（r407）

| 层 | 尺寸 | 关键偏移 | 本轮补充 |
|---|---|---|---|
| DM 包 | 80B | opcode 0x66 @+0x0c，wire_id @+0x48，pid @+0x4c | 透传 TA VA（@+0x28，r406 3D 布局类比） |
| TA 命令缓冲 | 360B | 内容为 TA 指令流 | **RGXSubmitTA 内无构造逻辑**；由客户端 3D 状态机生成 |
| 0x82:0xC IN | 268B | p_ta_cmd @120，kick_ta @188，ta_cmd_size @264 | p_ta_cmd 即 psKickTA[0] 的用户态 VA |

## 诚实边界

- **已确认**：调用链、VA 透传、PrepareTA 指针回填、psKickTA 无 UMD 侧构造函数（r193 corroborate）
- **推断**：+0x08/+0x10 语义、DM 包 @+0x28 为 TA VA（类比 3D 布局，未在 TA 路径逐字节验证）
- **未知**：360B TA 指令流的具体编码（TA ISA）；544B 缓冲内容；固件判定"真实负载"的最低条件
- 本轮未触碰硬件；反汇编为 Ghidra 伪 C，可能存在 decompiler  artifact（如 `RGXKickTA` 内 `local_180` 未初始化实为 `param_2` 溢出槽）

## r409 方案建议

既然 `RGXSubmitTA` 不提供构造逻辑，r409 有三条路（按推荐序）：

1. **真实应用 trace（推荐）**：找一个能跑起来的 3D 应用（哪怕是 MUSA SDK 的 triangle），用 r407 的 hook 机制捕获**真实** 360B 内容，回放即得最小真实包。前置：3D 应用能在当前 bridge 上走到 `RGXKickTA`。
2. **KMD/固件文档**：在 `/opt/MTT-driver-only/` 或内核驱动源码中找 TA 命令格式定义（如 `TA_TERMINATE`、PPP 状态头）。
3. **盲探（不推荐）**：按 PowerVR Rogue TA 架构猜测最小流（状态头 + TERMINATE），逐字节试探——r380 教训在前，DM3 试探风险需 pre-live 门禁覆盖。

r409 前置检查：确认有无可用 3D 应用；若无，路线 1 不可行，转路线 2。

## 交付

- 本报告：`mt-vgpu-guest/reports/r408-tasubmitta-no-construction.md`
- 门禁：`make check-offline` 全绿（纯分析，无代码改动）
- 本地提交（不 push）
