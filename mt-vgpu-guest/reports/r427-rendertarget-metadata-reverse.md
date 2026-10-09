# r427：Render Target 元数据结构逆向——psKickTA 全布局已测，结构本体为固件私有

> 轮次：r427（2026-10-09）。**纯离线反汇编。** r426 的方案 B（逆向结构复刻）。
> 背景：r426 确定 `+0x10` 指向 render-target 元数据结构；r425 超时因固件按结构解析像素数据。

## 结论

**psKickTA 的完整 18-qword 布局已 [MEASURED]（`FUN_0017d890`，decompiled.c:54347）；RTData 条目（0xD0 字节）的字段表已 [MEASURED]；但 `psKickTA[1]` 指向的 render-target 元数据结构本体是固件私有的——UMD 只做 VA 透传，其布局无法从 UMD 反汇编确定。**

方案 B 已达边界：结构复刻需要固件源码或真实 UMD 捕获。r428 必须换道。

## 反汇编证据

### 1. psKickTA 完整布局（`FUN_0017d890`，[MEASURED]）

`param_3` = psKickTA（qword 数组），`param_2` = TA kick 状态，`lVar3 = *(param_2+0xc)` = render context state，TA_buf = `*(param_2+0xb6)`。

| 索引 | 值 | 来源 |
|------|-----|------|
| [0] | RTData entry+0x08 | `*(param_2+0x1d2)` 缓存 |
| [1] | TA_buf+0x10 | = RTData entry+0x00（render target VA） |
| [2] | 上下文指针 | `*(param_2+0x1d0)` = `*(lVar3+0x3d8+idx*8)` |
| [3] | TA_buf+0x28 | = `*(lVar3+0x440)` |
| [4] | `0x3089705f3089705f` | **magic 常量**（UMD 写入） |
| [5] | 上下文状态 | `*(lVar3+0x10)` |
| [6] | TA_buf+0x18 | Header |
| [7] | TA_buf+0x20 | Header |
| [8] | TA_buf+0x48 | Header |
| [9] | TA_buf+0x40 | Header |
| [10] | TA_buf+0x30 | = `*(lVar3+0x448)` |
| [11] | TA_buf+0x38 | Header |
| [12] | `0x10` | 常量 |
| [13] | lo: TA_buf+0x68, hi: `(*param_2>>9)&8` | 混合 |
| [14] | lo: TA_buf+0x64, hi: TA_buf+0x60 | Header |
| [15] | lo: `*(lVar3+0x34)`, hi: `*(lVar3+0x18)` | 上下文 |
| [16] | lo: 0, hi: 0 或 TA_buf+0x124 | 条件 |
| [17] | lo: `(*param_2>>7)&0x40` | 标志 |

**关键**：`psKickTA[4]` 的 magic 由 UMD 的 SubmitTA 直接写入（非 Header 字段）。我们的 bridge 若自行构造 DM 包，必须确保该 magic 正确。

### 2. RTData 条目布局（0xD0 = 208 字节，[MEASURED]）

条目地址 = `lVar3 + 0x38 + idx*0xD0`（`idx = *(lVar3+0x24)`，`lVar3 = *(param_2+0xc)`）。

| 偏移 | 大小 | 内容 | 去向 | 证据 |
|------|------|------|------|------|
| +0x00 | 8 | render target VA | → TA_buf+0x10 → psKickTA[1] | RGXPrepareTA:52144 |
| +0x08 | 8 | 未知（缓存） | → psKickTA[0] | RGXPrepareTA:52104, SubmitTA:54360 |
| +0x48 | 8 | sync 计数器指针 | 内部 | RGXNextRTDataIsFree |
| +0x50 | 8 | render 计数器 | 内部 | 同上 |
| +0xC8 | 8 | 未知（缓存） | `param_2+0x1d4` | RGXPrepareTA:52105 |
| +0x118 | 8 | sync 指针 | 内部 | RGXRetrieveRenderTargetRendersInFlight |
| +0x120 | 8 | sync 计数器 | 内部 | 同上 |

条目数组基址 = `lVar3+0x38`（`RGXNextRTDataIsFree` 直接以 `param_1+idx*0xD0` 访问，`param_1` 即数组基址）。

### 3. Render Context State（`lVar3`）关键偏移（[MEASURED]）

| 偏移 | 内容 | 去向 |
|------|------|------|
| +0x10 | 8B 状态 | psKickTA[5] |
| +0x18 | 4B | psKickTA[15] 高 32 位 |
| +0x24 | 4B | buffer 索引（`puVar2[9]`） |
| +0x34 | 4B | psKickTA[15] 低 32 位 |
| +0x38 | RTData 数组基址 | 条目 = +0x38+idx*0xD0 |
| +0x3d8+idx*8 | 8B 指针数组 | psKickTA[2] |
| +0x440 | 8B | → TA_buf+0x28 → psKickTA[3] |
| +0x448 | 8B | → TA_buf+0x30 → psKickTA[10] |

### 4. Render Target 创建（`RGXAddRenderTarget`，decompiled.c:48989）

- 分配 `RGX_RT_ALLOCS`（0x160 字节主机内存）
- 调用 `FUN_001712a0` 创建"render target parameter memory"（设备内存，含 Parameter Buffer）
- 分配 MLIST、VHEAP 等设备内存区域
- 具体的元数据结构内容在设备内存中由 UMD 填写，**固件解析**

**未找到**：RTDataSet 的分配位置（`RGXAddRenderTarget` 的调用者在 UMD 之外，疑为 EGL 驱动）。

## 诚实边界

- psKickTA 布局、RTData 条目字段、context 偏移：**[MEASURED]**（逐行反汇编）
- `psKickTA[1]` 指向的结构布局：**[UNKNOWN]**（固件私有，UMD 只透传 VA）
- 固件对该结构的有效性检查逻辑：**[INFERRED]**（基于 r414/r425 行为对比）
- RTDataSet 分配位置：**未找到**（调用者在反汇编范围外）

## r428 前置条件

**方案 B 已达边界。** 剩余选项：

**选项 A（r426 推荐）：捕获真实 UMD 的 TA Header**
- 需要可运行的 Linux legacy UMD 环境
- 插桩 `FUN_00178800`，捕获 360B TA_buf
- 回放时重定位 VA

**选项 C（新）：3D 路径验证 T2**
- r41 已证明 3D 路径支持 render target 绑定与像素回读
- T2（像素可验证）可能经 3D 路径达成，无需 TA render target

**选项 D：最小结构试探**
- 构造最小元数据结构，观察固件行为
- **风险**：无依据试探，违反 r380/r418/r421/r425 教训；**不推荐**

**门禁**：r428 在 P0（选项 A 或 C 的离线准备）完成前**不得活体**。

## 本轮交付

- 门禁：`make check-offline` **550 Python + 1416 C 全绿**（1 skipped）；无代码变更
- 纯离线零硬件
