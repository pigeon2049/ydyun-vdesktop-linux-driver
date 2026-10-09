# r461：证据优先级审计——Linux 优先未导致字段错误，但遗漏 KMD 层盲点

**结论：r430–r459 的 360B Header 字段结论使用 Linux UMD 是正确的（Windows 驱动根本没有这些概念），未发现字段级错误。但存在两个"遗漏型错误"：r453 未检查 Windows KMD 是否添加 TA 命令；r457 未检查 KMD 是否填充 VM 信息。最大的代价是 KMD 层盲点直到 r460 才被识别。**

## 1. 审计方法

对 r430–r460 的 10 个关键结论，逐一检查：
1. 主要证据来自 Linux 还是 Windows？
2. Windows 驱动中是否有矛盾或支持证据？
3. 是否因"Linux 同系列更相关"假设而未充分检查 Windows？

## 2. 关键发现：架构根本差异

| 维度 | Linux UMD (linux-legacy-umd-5.2.0) | Windows UMD (mtdxum64.dll) |
|------|-------------------------------------|----------------------------|
| 架构术语 | RGX (643 引用) | MUSA (253 引用)，0 RGX |
| QuYuan 引用 | QuYuan1/2 Features (15 处) | 0 |
| TA 提交格式 | 360B Header + psKickTA[18] | 0x78B D3D11 kicks (15 qwords) |
| RgnHeader | 有 (InitRegionHeaderBuffer) | 无 (0 引用) |
| 360B Header | 有 | 无 |
| Tile 打包公式 | 有 (`((x+0x3f)>>6)`) | 无 |
| KMD 层 | 无 (直接 bridge) | 有 (mttkmd.sys) |
| RGXPrepareTA | 有 | 无 (0 引用) |
| psKickTA | 有 | 无 (0 引用) |

**根本原因**：两个驱动是不同的软件栈。Linux UMD 是 PowerVR RGX 风格，直接构造 360B header；Windows UMD 是 MUSA 风格，通过 D3D11 DDI 提交 kicks，由 KMD 处理。

## 3. 审计表：每个结论的证据来源

| 轮次 | 结论 | 主要证据 | Windows 证据状态 | 审计结果 |
|------|------|----------|------------------|----------|
| r430 | +0x10=RgnHeader VA | Linux UMD (明确声明 NOT in mtdxum64.dll) | 不适用 (Windows 无此概念) | ✓ 正确 |
| r437 | +0x28/+0x30=0 | Linux UMD | 不适用 | ✓ 正确 |
| r439 | +0x50/+0x58 tile 打包 | Linux UMD:58215 | 不适用 | ✓ 正确 |
| r441 | +0x120 11-bit 位表 | Linux UMD | 不适用 | ✓ 正确 |
| r442 | +0x68=(flags&3)==3 | Linux UMD | 不适用 | ✓ 正确 |
| r452 | +0x68 DDK 位定义 UNKNOWN | Linux UMD (无构造点) | 未检查 | ⚠️ 可改进 |
| r453 | TA 命令流=360B，无追加 | Linux RGXSubmitTA | 未检查 KMD | ⚠️ **遗漏型错误** |
| r454 | RgnHeader 双循环 128 dwords | Linux InitRegionHeaderBuffer | 不适用 (Windows UMD 无) | ✓ 正确 |
| r457 | TA 包缺 VM 信息 → MMU fault | Linux 包构造器对比 | 未检查 KMD 填充 | ⚠️ **遗漏型错误** |
| r460 | Windows 三层 vs Linux 直连 | Windows mtdxum64.dll | ✓ 首次深入分析 | ✓ 正确但延迟 |

## 4. 发现的错误

### 错误 1 (r453)：未检查 Windows KMD 是否添加 TA 命令 [严重性：中]

**Linux 证据**：
```c
// RGXSubmitTA (FUN_001796b0)
ui32TACmdSize = -(uint)(lVar23 != 0) & 0x168;  // 360 或 0
```
结论："360B header 就是完整的 TA DM 命令，无追加 TA 命令" → "TA 命令流方向已排除"

**未检查的 Windows 证据**：
- Windows UMD 提交 0x78B kicks (15 qwords) 给 KMD
- KMD (mttkmd.sys) 处理后向固件发送什么？**未知** (未反编译 KMD)
- KMD 可能添加 TA 命令、填充 RgnHeader、设置 VM 信息

**潜在影响**：
如果固件期望 KMD 添加的命令/结构，我方直接提交 360B 是不完整的。这可以解释为什么 360B header "正确"但固件仍 hang。

**验证方法**：反编译 mttkmd.sys，追踪 0x78B kick 处理流程。

### 错误 2 (r457)：未检查 KMD 是否填充 VM 信息 [严重性：中]

**Linux 证据**：
- 参考包构造器 `mt_work_command_encode()` 填写 +0x18 (root_pa)、+0x20 (process_id)
- TA 路径 `mt_fw_ta_real_command()` 留零 → 推断 MMU fault → hang
- r458 实现修复，r459 活体证伪 (行为无变化)

**未检查的 Windows 证据**：
- Windows 路径：UMD → KMD → 固件
- KMD 是否负责填充 VM 信息？**未知**
- 如果 KMD 填充 VM 信息，说明 VM 信息确实必要，但我方 r458 修复无效暗示问题不在此

**潜在影响**：
r459 证伪 MMU fault 假说后，我们排除了包内容方向。但如果 KMD 做了其他关键处理 (如 RgnHeader 构造、同步原语设置)，我们仍未覆盖。

**验证方法**：同错误 1，需 KMD 反编译。

### 非错误但延迟：KMD 层盲点 (r430–r459) [严重性：高，机会成本]

**事实**：
- r430 明确声明证据 "not in mtdxum64.dll" 后，r431–r459 几乎完全未再分析 Windows 驱动
- 直到 r460 (用户要求) 才建立 Windows 流程图，识别 "我方直连模型可能缺失 KMD 等效功能"

**机会成本**：
如果 r430 阶段就对比 Windows 三层模型 (UMD→KMD→固件) vs 我方直连 (UMD→Bridge→固件)，可能更早质疑：
- 直接 bridge 是否充分？
- KMD 做了哪些我们没做的事？
- 是否需要实现 KMD 等效功能？

这 30 轮 (r430–r460) 的试错可能部分避免。

## 5. 不需要修正的结论

以下结论使用 Linux UMD 是正确的，Windows 驱动没有这些概念，检查 Windows 也不会改变结论：

- **r430** (+0x10=RgnHeader VA)：Windows 无 360B header，无法提供证据
- **r437** (+0x28/+0x30=0)：Linux 特有布局
- **r439** (tile 打包公式)：Windows 无此公式
- **r441** (+0x120 11-bit)：Windows 用 0x78B 格式，无对应字段
- **r454** (RgnHeader 双循环)：Windows UMD 无 RgnHeader 概念 (0 引用)

**用户担忧的"部分实际上是错误的"**：在字段级结论上，**未发现错误**。Linux UMD 是 360B header 的正确参考源。

## 6. 需要用 Windows 证据重新验证的清单

### P0：反编译 Windows KMD (mttkmd.sys)
- [ ] KMD 收到 0x78B kick 后的完整处理流程
- [ ] KMD 是否构造 RgnHeader 或等效的 region 管理结构？
- [ ] KMD 是否填充 VM 信息 (root_pa/token) 到固件命令？
- [ ] KMD 向固件发送的最终命令格式 (是否仍是 360B？还是其他？)
- [ ] KMD 的 fence/sync 处理 vs 我方 bridge fence

### P1：Windows UMD→KMD 接口
- [ ] D3DDDIRenderCb 的完整参数 (除 0x78B kicks 外还有什么？)
- [ ] D3DDDIEscapeCb 私有通道传输什么数据？(可能是关键配置！)

### P2：直接提交路径
- [ ] Windows 驱动是否有 bypass KMD 的直接提交路径？如果有，对比其包格式

## 7. 修正建议

1. **短期 (r462)**：启动 Windows KMD (mttkmd.sys) 反编译，重点追踪 kick 处理流程
2. **中期**：建立 "KMD 等效功能清单"——Windows KMD 做的每件事，检查我方是否覆盖
3. **长期**：考虑是否需要在 Bridge 层实现 KMD 的关键功能，而非继续调优 360B header 字段
4. **方法论**：对于"固件行为"类问题，优先以"真实工作的驱动" (Windows+KMD) 为基准，而非仅以"同系列" (Linux UMD) 为基准。同系列保证格式正确，真实工作保证行为正确。

## 8. 诚实边界

- [MEASURED]：Linux/Windows 驱动的术语统计 (RGX/MUSA/QuYuan 引用数)；r430–r459 报告的证据来源；Windows 0x78B kick 结构 (r460)
- [INFERRED]：KMD 可能添加 TA 命令/填充 VM 信息 (未反编译 KMD，基于架构推理)
- [UNKNOWN]：Windows KMD 内部处理；mttkmd.sys 是否有 decompiled.c (当前只有 .sys 文件，无反编译输出)；两个驱动是否针对完全相同的 S3000 硬件版本
- 本轮纯离线，零硬件触碰；**未链入下一轮**
