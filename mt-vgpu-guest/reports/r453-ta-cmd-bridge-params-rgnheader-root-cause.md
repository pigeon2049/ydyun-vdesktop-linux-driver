# r453：TA 命令流与 Bridge 参数检查——固件超时的最可能根因是未填充的 RgnHeader（离线）

**结论：UMD 的 TA 命令就是 360B header 本体，无追加命令——我方 header-only 与 UMD 一致，TA 命令流不是问题。固件确实在读我们的 header（零=快路径，非零=尝试执行→hang）。最可能根因 [INFERRED 高置信]：我们声明"有 TA 工作"但 RgnHeader（0x7c000000）只是 dword 1s 初始化值、从未填入有效 region 数据，固件解析垃圾 region header 时 hang。**

## 1. TA 命令流分析 [MEASURED]

**UMD 只发送 360B，无追加 TA 命令。**

`RGXSubmitTA` (FUN_001796b0) 构造 bridge 调用时：
```c
&local_548, local_4d0, -(uint)(lVar23 != 0) & 0x168, lVar23,
//                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//                    ui32TACmdSize = 0x168 (360) if TA buffer exists, else 0
```

`-(uint)(lVar23 != 0) & 0x168`：当 TA buffer 指针 (`lVar23`) 非空时，size = 0x168 (360)；否则为 0。

这证实：
- **360B header 就是完整的 TA DM 命令** (`pui8TADMCmd`)，UMD 不在其后追加任何 TA 命令
- 我方 `mt_ta_submit_real()` 的 header-only 做法与 UMD **完全一致**
- **TA 命令流不是超时根因**——r452 的"检查 TA 命令流"方向已排除

## 2. Bridge 参数对照

### 我方 `mt_ta_submit_real()` 设置的参数

| 参数 | 我方值 | 说明 |
|---|---|---|
| `ta_cmd_va` | 360B header VA | ✓ 与 UMD 一致 |
| `ta_cmd_size` | 360 (`MT_TA_CMD_BUFFER_BYTES`) | ✓ 与 UMD 一致 (r363 实测也是 360) |
| `kick_flags` | 0 | 无 TA/PR 标志 |
| `ta_upd_count` | 0 | `mt_ta_submit_validate` **拒绝** >0 |
| `ta_fence_count` | 0 | `mt_ta_submit_validate` **拒绝** >0 |
| `check_fence` | 0 | 无依赖 |

### 真实 UMD `RGXKickTA3D` 的完整参数 (official-vgpu `rgxta3d.h`)

真实 kick 包含我方**完全没有**的参数：
- Client TA fence sync prims (`apsClientTAFenceSyncPrimBlock`, count, offset, value)
- Client TA update sync prims
- Client 3D update sync prims
- PR fence (`psPRSyncPrimBlock`, offset, value)
- Render target dataset (`psKMHWRTDataSet` — 完整的 RT 数据集，不只是 RgnHeader VA)
- Z-buffer / MSAA scratch (`psZSBuffer`, `psMSAAScratchBuffer`)
- Sync PMRs (`ppsSyncPMRs`, count, flags)
- Draw call / index / MRT counts
- Fence names, timelines, deadline

**评估**：我方的 validation 主动拒绝非零 sync counts，这是**刻意的最小化测试策略**。对于最小 TA kick，固件应能处理零 sync prims——r414 的零 header 能完成证明固件接受最小参数集。**Bridge 参数缺失不是 hang 的直接原因** [INFERRED 中置信]。

## 3. 固件命令包 (0x50B) 的正确性问题 [MEASURED]

### 我方发送的包 (`mt_fw_ta_real_command`)

```
+0x0c: 0x66        (opcode, MT_FW_TA_OPCODE)
+0x28/+0x2c: TA buffer VA (64-bit)
+0x30: TA buffer size
+0x48: wire_id
+0x4c: pid
```

### 真实 KCCB TA Kick (official-vgpu `rgx_fwif_km.h`)

```c
RGXFWIF_KCCB_CMD_KICK = 101U | RGX_CMD_MAGIC_DWORD_SHIFTED
// = 101 | (0x2ABC << 16) = 0x2ABC0065
// "DM workload kick command"

typedef struct {
    PRGXFWIF_FWCOMMONCONTEXT psContext;  // 固件上下文指针！
    IMG_UINT32 ui32CWoffUpdate;
    IMG_UINT32 ui32CWrapMaskUpdate;
    IMG_UINT32 ui32NumCleanupCtl;
    PRGXFWIF_CLEANUP_CTL apsCleanupCtl[...];
    IMG_UINT32 ui32WorkEstCmdHeaderOffset;
} RGXFWIF_KCCB_CMD_KICK_DATA;
```

### 关键发现

1. **我方 opcode 0x66 ≠ 真实 KICK (0x2ABC0065)** [MEASURED]
   - 0x66 = 102 decimal = `RGXFWIF_KCCB_CMD_MMUCACHE` 的命令号（**无 magic dword**）
   - 我方 0x66 来自 `mt_work_opcode()` case 3/11 ("RGXVertex/UniversalQueue")——这是 **work-queue 命名空间**，不是 KCCB 命令命名空间
   - D4 注释承认这是 INFERRED："If the firmware NAKs it, capture the Windows KMD's TA opcode as follow-up"

2. **"MEASURED" 的包布局证据已动摇** [LOGIC]
   - `mt_marker_fence.h` 注释："[MEASURED]: TA buffer VA @+0x28/+0x2c, size @+0x30 (r414 live: firmware 0x100 COMPLETED, 219us). Was [INFERRED] by 3D analogy (r381); promoted r415."
   - 但 r425 已证明 r414 是固件"无工作"快路径（全零 header），**不是真实 TA 执行**
   - 因此 VA@+0x28/size@+0x30 布局**从未在真实 TA 工作下被验证**——"MEASURED" 标签建立在已证伪的证据上

3. **但包格式可能不是 hang 的原因** [INFERRED]
   - 如果 host proxy 拒绝 0x66，我们应得立即错误，而非 5s 超时
   - 超时意味着命令被接受、固件尝试执行但未完成
   - r414 的 0x100 完成（含我们的 wire_id）证明 proxy 处理了我们的包格式

## 4. 最可能根因：未填充的 RgnHeader [INFERRED 高置信]

### 证据链

1. **固件在读我们的 360B header** [INFERRED 高置信]
   - 全零 header → 219µs 完成（"无工作"快路径）
   - 非零 header → 5s 超时（尝试执行）
   - 行为差异证明固件解析了 header 内容

2. **我们的 header 声明"有工作"**
   - `+0x10` = RgnHeader VA (0x7c000000)
   - `+0x50/+0x58` = 1x1 tiles
   - `+0x120` = 0x1 (bit0 = 有 TA 工作)

3. **但 RgnHeader 内容无效** [MEASURED]
   - `kernel/recovery/mt_pvr_bridge.c:4260-4284`：RgnHeader BO 填充为每 dword `0x00000001`
   - 这复制了 UMD `InitRegionHeaderBuffer` 的**初始化** (`*local_690[0] = 1`)
   - 但 UMD 在初始化后会填入**真实的 region 数据**（tile 坐标、object 列表等）
   - 我们从未填充——固件读到的是 0x00000001 垃圾

4. **固件 hang 机制** [INFERRED]
   - 固件看到"有 TA 工作"，读取 0x7c000000 的 RgnHeader
   - 尝试将 0x00000001 dwords 解析为 region header 结构
   - 得到无意义的 tile/object 指针，可能进入无限循环或等待不存在的同步对象
   - 全零 header 时固件根本不读 RgnHeader → 快速完成

### 为什么不是其他原因

| 候选 | 排除理由 | 证据等级 |
|---|---|---|
| TA 命令流缺失 | UMD 也只发 360B，无追加命令 | [MEASURED] 排除 |
| `+0x68` 错误 | r452 已分析，写 0 可能正确 | [INFERRED] 低嫌疑 |
| `+0x120` 其他位 | bit0=1 是"有工作"标志，与 hang 一致；其余位未知但非主因 | [INFERRED] 低嫌疑 |
| Bridge 参数缺失 | 最小参数集应可工作；r414 证明接受 | [INFERRED] 低嫌疑 |
| 0x50B 包 opcode 错误 | 若被拒绝应立即错误而非超时 | [INFERRED] 低嫌疑 |
| **RgnHeader 无效** | **唯一与"非零 hang / 全零完成"模式完全吻合的解释** | **[INFERRED] 高置信** |

## 5. 下一步建议

1. **P0**：研究 UMD 在 `InitRegionHeaderBuffer` 之后填入 RgnHeader 的真实内容——最小有效 region header 的格式是什么？1x1 tile、无 draw 时哪些字段必须有效？
2. **P1**：如果完整 RgnHeader 难以构造，考虑是否可以用全零 RgnHeader + 非零 header 其他字段做"有工作但无 tile"的测试，观察固件行为差异。
3. **P2**：0x50B 包的 opcode 问题（0x66 vs 0x2ABC0065）值得记录为 [TO-VALIDATE]，但当前证据指向 RgnHeader 而非包格式。
4. **不建议**：继续调整 header 偏移量——r422-r452 已穷尽 header 布局，问题在 header **引用**的内容而非 header 本身。

## 诚实边界

- [MEASURED]：UMD 只发 360B；真实 KCCB KICK=0x2ABC0065；我方 opcode=0x66；RgnHeader 填 dword 1s；r414 快路径 vs r418+ 超时的行为差异
- [INFERRED 高置信]：固件 hang 是因为解析无效 RgnHeader
- [INFERRED 中置信]：Bridge 参数缺失、包 opcode 不是主因
- [UNKNOWN]：host proxy 对 0x66 的具体处理；固件 hang 的精确位置；最小有效 RgnHeader 格式
- 本轮纯离线，零硬件触碰，无生产代码变更
