# r410：Windows 驱动挖掘——TA ISA 字段语义与 Linux 桥差异

> 轮次：r410（2026-10-09）。**纯离线。**
> 背景：用户指示优先参考 Windows 驱动；r409 确认标准程序不可达、TA 缓冲结构已解析但 qword 语义未知。

## 结论

**Windows 驱动为纯二进制**（`/opt/MTT-driver-only/` 仅含 DLL/SYS，无头文件、无文档），TA ISA 信息来自 Linux UMD 反汇编（`decompiled/linux-legacy-umd-5.2.0/decompiled.c`）。本轮完成：

1. **TA 缓冲 9 qword 字段语义表**（40B/72B 条目，来源：`FUN_00169240`）
2. **Linux 桥差异点**：当前仅发 80B marker，缺 360B VA/size/DMA 映射/条目构造
3. **真实 TA 包前置条件清单**（4 项）

## 1. Windows 驱动梳理结果

| 位置 | 内容 | TA 相关 |
|---|---|---|
| `/opt/MTT-driver-only/` | 24 个 DLL/SYS（二进制） | 无头文件、无 `.md`、无示例代码 |
| 字符串扫描 | `mtdxum64.dll`、`mtapi64.dll` | 无 "TA state buffer"（已剥离或不同术语） |
| `decompiled/linux-legacy-umd-5.2.0/` | Ghidra 伪 C（606069 行） | **主要信息源** |

**诚实边界**：Windows 驱动二进制无法提供 TA ISA 文档；以下语义来自 Linux UMD 反汇编，可能有 decompiler artifact。

## 2. TA 缓冲字段语义表

**来源**：`FUN_00169240`（decompiled.c:43159），填充循环 `while (local_d4 < param_2[0xe])`

**条目结构**：
- 简单条目：5 qwords = 40B（`bVar31 == true`）
- 复杂条目：9 qwords = 72B（`bVar31 == false`）
- 指针：`render_ctx+0xb6`，写后推进 0x28 或 0x48

### 2.1 简单条目（5 qwords）

| Qword | 变量 | 语义 | 来源 |
|---|---|---|---|
| Q0 | `local_90` | 地址/标志。`uVar16` 低 32 位 + `(uVar16>>0x20)` 中 16 位 + `(uVar16>>0x30)` 高 16 位；或 `uVar15 \| uVar17 & 0xfff8001f9fffffff \| 0x48000000000` | decompiled.c:44293-44310 |
| Q1 | `uStack_88` | `*(param_1+0x10)`（8B）；byte7 = `(uVar17>>0x30) & 0xfe \| (param_1[0x18] & 1)` | decompiled.c:44299-44303 |
| Q2 | `local_80` | 打包维度：`((w-1) & 0x7fff) << 0x29 \| ((h-1) & 0x7fff) << 0x1a`；byte7 = `(param_1[0x14] << 6) \| (Q2._7_1_ & 0x3f)` | decompiled.c:44304-44312 |
| Q3 | `uStack_78` | `*(lVar29+8)`（8B）；byte6 = `(uVar17>>0x30) & 0xfe \| (*(lVar29+0x28) & 1)` | decompiled.c:44313-44317 |
| Q4 | `local_70` | 维度乘积 `(w*h-1)` 或打包 `((w-1) & 0x7fff) << 0x29 \| ((h-1) & 0x7fff) << 0x1a`；byte7 = `(*(lVar29+0x18) << 6)` | decompiled.c:44318-44332 |

### 2.2 复杂条目附加（4 qwords）

| Qword | 变量 | 语义 | 来源 |
|---|---|---|---|
| Q5 | `uStack_68` | Scissor：`(x & 0x7fff) << 0x20 \| (y & 0x7fff) << 0x10` | decompiled.c:44364-44365 |
| Q6 | `local_60` | Viewport：`((w-1) & 0x7fff) << 0x10 \| ((h-1) & 0x7fff)` | decompiled.c:44370-44372 |
| Q7 | `uStack_58` | Scissor：`(x & 0x7fff) << 0x20 \| (y & 0x7fff) << 0x10` | decompiled.c:44362-44363 |
| Q8 | `local_50` | Viewport：`((w-1) & 0x7fff) << 0x10 \| ((h-1) & 0x7fff)` | decompiled.c:44366-44369 |

### 2.3 RGXPrepareTA 回读验证

`RGXPrepareTA`（decompiled.c:54365-54374）从缓冲读取字段到 `psKickTA`，确认布局：

| psKickTA 偏移 | 缓冲偏移 | 用途 |
|---|---|---|
| +0x08 | +0x10 (Q2) | 已确认 |
| +0x18 | +0x28 (Q5) | 复杂条目 |
| +0x30 | +0x30 (Q6) | 复杂条目 |
| +0x38 | +0x38 (Q7) | 复杂条目 |
| +0x40 | +0x40 (Q8) | 复杂条目 |
| +0x48 | +0x48 (Q9) | 超出 72B，待确认 |

## 3. 与 Linux 桥的差异点

**当前 Linux 路径**（`kernel/mt_marker_fence.h:538` `mt_ta_submit_build`）：

```c
mt_fw_ta_marker_command(packet, wire_id, pid);  // 仅 80B marker
```

**缺失**：

| # | 缺失项 | 说明 |
|---|---|---|
| 1 | 360B 缓冲 VA | DM 包需含 TA 缓冲的 device VA（`psKickTA[0]`） |
| 2 | 360B size | 固定 0x168，需在包中声明 |
| 3 | DMA 可见映射 | 缓冲需 `dma_alloc_coherent` 或 VM 绑定到 GPU 可见 VA |
| 4 | 条目构造 | 需实现 `FUN_00169240` 等价逻辑（40B/72B 条目填充） |
| 5 | `psKickTA` 结构 | 需定义 268B IN 中的 `p_ta_cmd`/`ta_cmd_size` 映射 |

## 4. 构造真实 TA 包的前置条件

- [ ] **P1**：确认固件期望的 DM 包布局（80B 后追加 VA/size？还是独立描述符？）
- [ ] **P1**：实现 360B 缓冲的 DMA 分配与 GPU VA 映射
- [ ] **P2**：实现最小条目构造（至少 1 个简单条目，40B）
- [ ] **P3**：验证 `RGXPrepareTA` 回读路径（可选，用于调试）

## 交付物

- 本报告 `reports/r410-windows-ta-isa.md`
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r410
- `PROGRESS-SNAPSHOT.md` §12 追加 r410

## 门禁

- `make -C mt-vgpu-guest check-offline`：待跑
- 纯离线，零内核改动

## 诚实边界

- Windows 驱动为二进制，无文档；语义来自 Linux UMD 反汇编
- Ghidra 伪 C 可能有 decompiler artifact
- Qword 语义为"已确认存在"级别，部分位域含义为推断
- 未做活体验证
