# r426：`+0x10` 指向 render-target 元数据结构——真实 Header 需要 UMD 构建的上下文状态

> 轮次：r426（2026-10-09）。**纯离线反汇编。** r425 超时的深层原因定位。
> 背景：r425 Header-only 仍超时；r414 全零=固件"无工作"快路径（假阳性）。决策：必须离线确定真实 Header 语义。

## 结论

**`TA_buf+0x10` 的值是 `*(render_ctx + idx*0xD0 + 0x38)`——一个由 UMD 在 render context 创建时分配并初始化的 render-target 元数据结构的设备地址，不是原始像素 BO。** 我们的 Header-only 方案把 16KB 像素 BO 的 VA 写到 `+0x10`，固件将其当作元数据结构解析 → 垃圾 → 挂起。

**真实 Header 的大多数字段指向 UMD 管理的上下文内部状态（render_ctx+0x440/0x448、per-buffer 描述符数组等），无法从零凭空构造。** r427 必须换道：捕获真实 UMD 的 TA Header 并回放，或深入 UMD 的 render context 初始化。

## 反汇编证据

### 1. RGXPrepareTA 完整写入清单（`FUN_00178800`，decompiled.c:52055，[MEASURED]）

TA 缓冲基址：`lVar8 = *(param_2 + 0xb6)`（param_2 = render context state）。

**8 字节写入：**

| 偏移 | 值 | 来源 |
|------|-----|------|
| +0x10 | uVar10 | `*(render_ctx + idx*0xD0 + 0x38)`（见 §2） |
| +0x28 | `*(param_2 + 0x1cc)` | `*(render_ctx + 0x440)` |
| +0x30 | `*(param_2 + 0x1ce)` | `*(render_ctx + 0x448)` |
| +0x78 | `*(puVar2 + idx*0x34 + 0x32)` | 条件（features） |
| +0xb0 | `*(puVar2 + 0x11c)` | 条件 |
| +0xe8 | `*(puVar2 + 0x162)` | 条件 |
| +0x80/0xb8/0xf0 … | 各 RT 描述符 | 条件（多 render target） |

**4 字节写入：**

| 偏移 | 值 | 来源 |
|------|-----|------|
| +0x68 | `(uint)((*param_2 & 3) == 3)` | 布尔标志 |
| +0x120 | 位打包 | `*param_2` 的标志位（逐位 OR） |
| +0x140/0x150/0x160 | 0 或特征值 | 条件（GetFeatures） |

**16 字节写入：**

| 偏移 | 方式 |
|------|------|
| +0x50 | `FUN_00184220(uVar13, 0, ...)` |
| +0x58 | `FUN_00184220(uVar4, 1, ...)` |

### 2. `+0x10` 的真实语义（[MEASURED]，逐行追踪）

```c
// decompiled.c:52114-52120
lVar6 = GetFeatures(param_1);
if (*(uint *)(lVar6 + 0x54) < 2) {
  uVar10 = *(undefined8 *)(puVar2 + (ulong)puVar2[9] * 0x34 + 0xe);
}
else {
  uVar10 = *(undefined8 *)(param_2 + 0x1c8);
}
*(undefined8 *)(lVar8 + 0x10) = uVar10;
```

其中：
- `puVar2 = *(param_2 + 0xc)` = render context 基址（`uint*`）
- `puVar2[9]` = `*(render_ctx + 0x24)` = 当前 buffer 索引（dword）
- Path A：`(puVar2[9] * 0x34 + 0xe)` 以 uint 为单位 → 字节偏移 = `idx * 0xD0 + 0x38`
- Path B：`*(param_2 + 0x1c8)`，而 `*(param_2 + 0x1c8) = *puVar14`，`puVar14 = render_ctx + 0x38 + idx * 0xD0`（decompiled.c:52093-52099）

**两条路径收敛：`+0x10` = `*(render_ctx + idx * 0xD0 + 0x38)`。**

这是一个 **per-buffer 描述符数组**（每个 0xD0=208 字节，`idx` 为当前 buffer 索引），在偏移 `0x38` 处存放 8 字节设备地址。

**该地址的语义**：render-target 元数据结构的 VA。PowerVR 的 render target 不是裸像素缓冲，而是一个固件可解析的结构（含颜色/深度缓冲地址、tile 配置、ISP 状态指针等）。该结构由 UMD 在 render context 创建期间分配并填写到描述符数组的 `+0x38` 处。

**状态**：地址来源 [MEASURED]；目标结构的具体布局 [UNKNOWN]（固件私有）。

### 3. 固件必需字段（`FUN_0017d890`，decompiled.c:54347，[MEASURED]）

psKickTA 构建（param_3 = psKickTA，TA_buf = `*(param_2 + 0xb6)`）：

| psKickTA 槽位 | 来源 | 备注 |
|---|---|---|
| [1] (+0x08) | `*(TA_buf + 0x10)` | render target 结构 VA |
| [3] (+0x18) | `*(TA_buf + 0x28)` | `*(render_ctx+0x440)` |
| [10] (+0x50) | `*(TA_buf + 0x30)` | `*(render_ctx+0x448)` |
| [4] (+0x20) | `0x3089705f3089705f` | **magic 常量**（UMD 写入，非 Header 字段） |
| [6] (+0x30) | `*(TA_buf + 0x18)` | Header |
| [7] (+0x38) | `*(TA_buf + 0x20)` | Header |
| [8] (+0x40) | `*(TA_buf + 0x48)` | Header |
| [9] (+0x48) | `*(TA_buf + 0x40)` | Header |
| [11] (+0x58) | `*(TA_buf + 0x38)` | Header |

**固件行为模型**（[INFERRED]，基于 r414/r425 对比）：
- `psKickTA[1] == 0` → "无工作"快路径 → 立即完成（r414，219µs）
- `psKickTA[1] != 0` → 尝试解析 render target 结构 → 若结构无效 → 挂起（r425）
- `psKickTA[4]` 的 magic 由 UMD 的 SubmitTA 写入，用于 kick 合法性校验（我们的 bridge 必须确保正确设置）

### 4. 为什么 Header-only 必然失败

| 轮次 | `+0x10` 的值 | 固件解读 | 结果 |
|------|-------------|----------|------|
| r414 | 0x0 | "无工作" | ✅ 219µs |
| r425 | 0x7b000000（16KB 像素 BO） | render target 结构 → 解析垃圾 | ❌ 超时 |

**我们的 16KB BO 是裸像素缓冲，不是 render target 元数据结构。** 固件按结构布局去读 `0x7b000000` 处的内容，得到的是全零像素数据（或未初始化内存），不是有效的结构 → 挂起。

**更深层**：即使 `+0x10` 正确，`+0x28`（`*(render_ctx+0x440)`）和 `+0x30`（`*(render_ctx+0x448)`）也指向 UMD 的上下文内部状态，我们全部填零。固件很可能需要这些字段有效。

## 真实 Header 字段表

| 偏移 | 大小 | 值（真实 UMD） | 来源 | 状态 |
|------|------|---------------|------|------|
| +0x10 | 8 | render target 结构 VA | `*(render_ctx+idx*0xD0+0x38)` | [MEASURED] 来源，语义 [INFERRED] |
| +0x28 | 8 | 上下文状态 | `*(render_ctx+0x440)` | [MEASURED] |
| +0x30 | 8 | 上下文状态 | `*(render_ctx+0x448)` | [MEASURED] |
| +0x68 | 4 | 布尔标志 | `(*param_2 & 3) == 3` | [MEASURED] |
| +0x78/0xb0/0xe8 | 8 | RT 描述符 | per-buffer 数组 | [MEASURED] 存在，条件 |
| +0x120 | 4 | 位打包标志 | `*param_2` | [MEASURED] |
| +0x140/0x150/0x160 | 4 | 特征相关 | GetFeatures | [MEASURED]，条件 |

## r427 前置条件

**P0（二选一，必须离线完成其一才能活体）：**

**方案 A：捕获回放（推荐）**
1. 在 x86 或现有环境运行真实 Linux legacy UMD（`decompiled/` 有完整反汇编，或找原 .so）
2. 插桩 `FUN_00178800`，捕获它写入 TA_buf 的完整 360B
3. 我们的 bridge 直接回放这些字节（将 VA 重定位到我们的 BO）
4. 风险：VA 重定位需要理解每个指针字段；但比从零构造可靠得多

**方案 B：Render context 逆向**
1. 逆向 UMD 的 render context 创建路径，找到 `render_ctx+idx*0xD0+0x38` 的分配与初始化
2. 在我们的 bridge 中复刻最小 render target 结构
3. 风险：结构布局完全未知，工作量大

**P1（无论哪个方案）：**
- 确认 `mt_pvr_bridge` 的 SubmitTA 路径正确设置 `psKickTA[4] = 0x3089705f3089705f`
- 检查 DM 包中 psKickTA 的其他字段是否需要非零值

**门禁**：r427 在 P0 完成前**不得活体**（r380/r418/r421/r425 教训）。

## 诚实边界

- `+0x10` 的地址来源已 [MEASURED] 追踪到 `render_ctx+idx*0xD0+0x38`；目标结构布局 [UNKNOWN]
- render target 结构的具体内容（颜色缓冲地址在哪、tile 配置在哪）完全未知
- 固件的"有效性检查"逻辑为 [INFERRED]（基于行为对比，非代码实证）
- 本轮零硬件触碰，纯离线反汇编；Ghidra 伪 C 或有 decompiler artifact
- 行号以 `decompiled/linux-legacy-umd-5.2.0/decompiled.c` 为准

## 门禁

- `make -C mt-vgpu-guest check-offline`：待跑（本轮纯分析，无代码变更）
- 本轮无代码变更，无需 `make kernel`

## 交付物

- 本报告 `reports/r426-plus0x10-render-target-metadata.md`
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r426（§4 清理）
- `PROGRESS-SNAPSHOT.md` §12 追加 r426
- 本地提交（不 push）
