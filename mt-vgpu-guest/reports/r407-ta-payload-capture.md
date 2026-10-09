# r407：TA 命令缓冲捕获机制验证——live hook 成功，真实布局待静态分析

> 轮次：r407（2026-10-09）。**活体观察型，低风险。**
> 背景：r405 路线图第二步；r406 证明固件需真实 payload；r363 捕获 IN 参数但未捕获命令缓冲内容。

## 结论

**TA 命令缓冲 live 捕获机制验证成功；真实 UMD TA 负载布局需静态分析。**

- 桥侧临时 hook（`copy_from_user` + `print_hex_dump`）成功捕获 0x82:0xC 的 TA 命令缓冲前 64 字节
- 捕获的是 fabricated 测试模式（非真实 UMD 负载），机制本身已验证
- 真实 TA 负载布局：DM 包 80 字节（opcode 0x66 @+0x0c，wire_id @+0x48，pid @+0x4c）；TA 命令缓冲 360 字节（r363 实测），内容为 TA 指令流，需反汇编 UMD 的 RGXSubmitTA 进一步解析
- Hook 已移除，源码恢复，桥已重载为干净构建

## 活体证据

```
[ 1060.736700] mt_pvr_bridge: musakickgfx2 dispatch: ctx=0x0 abort=0 kick_ta=1 kick_3d=0 kick_pr=0 ta_size=360 ...
[ 1060.736707] mt_pvr_bridge: r407: TA cmd va=0x7ffe514c5970 size=360:
[ 1060.736709] r407 TA: 00000000: 66 00 68 01 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af
[ 1060.736711] r407 TA: 00000010: a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af
```

- `ta_size=360` 与 r363 实测一致
- Hook 在 render_ctx 检查前运行，observer 模式不影响 dispatch
- 测试后返回 -EINVAL（无 context，符合 r398 预期）

## TA 包布局（已知）

| 层 | 尺寸 | 关键偏移 | 来源 |
|---|---|---|---|
| DM 包 | 80B (`MT_FW_COMMAND_BYTES`) | opcode 0x66 @+0x0c，wire_id @+0x48，pid @+0x4c | `mt_marker_fence.h:318`，r365 活体验证 |
| TA 命令缓冲 | 360B (r363) | 内容为 TA 指令流，布局未知 | r363 IN 观察 |
| 0x82:0xC IN | 268B | p_ta_cmd @120，kick_ta @188，ta_cmd_size @264 | `mt_pvr_wire.h` static_assert |

## 诚实边界

- **捕获的是 fabricated 模式**，非真实 UMD 生成的 TA 负载（无 3D 应用可用）
- 真实 TA 指令流布局未解析（UMD `RGXSubmitTA` 反汇编复杂，需专项分析）
- Hook 为一次性代码，已从源码移除，未提交
- 本轮未提交任何 GPU 工作（observer 模式）

## 系统状态

- Pre-live T1/T2/T3 全过
- Probe: trial restored, bound to 00:0e.0
- Bridge: 重载干净构建，ref=0
- dmesg: 无 WARN/BUG/Oops
- 无需冷重启（无 pending fence）

## 下一步（r408）

r405 路线图：TA 包解析（离线）——基于 UMD 反汇编分析真实 TA 指令流布局。
