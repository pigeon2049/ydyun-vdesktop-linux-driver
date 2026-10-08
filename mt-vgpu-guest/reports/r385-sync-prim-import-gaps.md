# r385：R7 Sync prim import——SYNC:0xC 未实现（-ENOTTY）；UMD TA 路径的潜在阻塞点（离线调研，零硬件触碰）

## 结论

**R7 的准确定义**（r355 原文）：
> **R7（Sync prim 导入路径）**：`ZeusSyncPrimImportFD` 下游在真实建连后应走通；桥侧 SYNC 命令多已实现，需 R1 完成后验证。

**核心发现**：`ZeusSyncPrimImportFD` 对应桥调用 **SYNC:0xC**（`BridgeSyncPrimImportFD`），但 Linux 桥侧**未实现**——`mt_pvr_wire.h` 无 `0xC` 的 `MT_PVR_FN_*` 定义，dispatch 的 `default:` 返回 `-ENOTTY`。

**现状三分法**（SYNC 组 0x2）：
| 命令 | 状态 | 实现 |
|---|---|---|
| 0x0 AllocSyncPrimitiveBlock | 真实 | `pvr_cmd_sync_block`：创建 `MT_PVR_KIND_SYNC` 对象 + PMR |
| 0xA SyncPrimCpuSignal | 真实 | `pvr_cmd_syncprim_set`（r222）：写 u32 到 sync PMR |
| 0x1/0x2/0x7/0x8 | 空桩 | `pvr_stub_ok`（Free/Set/AllocEvent/FreeEvent） |
| **0xC SyncPrimImportFD** | **缺失** | **无定义，走 `default: -ENOTTY`** |

**是否阻塞真实 UMD**：**很可能阻塞 TA 路径**。r361 证实 UMD TA 路径为 `... → ZeusSyncPrimImportFD → 0x92930(0x82:0xC)`；若 `BridgeSyncPrimImportFD` 返回 `-ENOTTY`，UMD 可能在到达 `0x82:0xC` 前中止。但**尚无活体证据**（r361 在 `SyncPrimRef` 处被阻塞，未到达 `ZeusSyncPrimImportFD`）。

**与 R6 的关系**：**独立**。R6 是 context 对象（render context 需 firmware 状态）；R7 是 sync prim 对象（FD 导入）。两者无依赖，但真实 UMD 渲染**同时需要**（R6 的 context BO + R7 的 sync prim 共享）。

## 1. R7 定义澄清

### 1.1 r355 原文与语境

r355 将 DDK2 backend 缺口分为 R1–R7，其中：
> **R7（Sync prim 导入路径）**：`ZeusSyncPrimImportFD` 下游在真实建连后应走通；桥侧 SYNC 命令多已实现，需 R1 完成后验证。

"Sync prim import" 具体指：
1. **Sync primitive**：GPU 同步原语（UFO - Update Fence Object），firmware 侧的同步计数器
2. **Import**：从文件描述符（FD）导入已存在的 sync prim（跨进程/跨设备共享）
3. **FD**：Linux 的 dma-buf/sync_file 风格 FD，或 PVR 私有的 sync FD

### 1.2 ZeusSyncPrimImportFD（UMD 侧）

位置：`decompiled/linux-legacy-umd-5.2.0/decompiled.c:51849`（`0x178310`）

```c
undefined8 ZeusSyncPrimImportFD(long *param_1, long param_2, undefined4 param_3, undefined8 param_4)
{
  if (param_2 == 0) return 3;  /* INVALID_PARAMS */
  if (*(int *)(param_2 + 8) != 1) return 0xf7;
  puVar1 = *(undefined8 **)(param_2 + 0x18);
  uVar2 = FUN_00139990(..., param_3, param_4);  /* BridgeSyncPrimImportFD */
  return uVar2;
}
```

`FUN_00139990`（`:12416`）：
```c
iVar1 = FUN_00192930(param_1, 2, 0xc, &local_40, 0x18, &local_4c);
/* PVRSRVBridgeCall(conn, group=2, func=0xc, in=24B, out) */
```

即：**SYNC 组（0x2），功能号 0xC**，IN 24B，OUT 8B（`local_4c`）。

### 1.3 在 TA 路径中的位置

r361 报告：
> "TA 路径无法推进到 `ZeusSyncPrimImportFD → 0x92930`"

r354 证据链：
```
0x79733 → 0x7a30b → 0x36ec0 → 0x37111 → 0x92930(rdi=0x6000, rsi=0x82, rdx=0xc)
```

即 UMD TA 路径：`SyncPrimRef` → `ZeusSyncPrimImportFD`（SYNC:0xC）→ ... → `0x82:0xC`（MUSAKICKGFX2）。

**[实测]**：反汇编与调用链（decompiled.c + r354/r361 报告）
**[推断]**：`ZeusSyncPrimImportFD` 是 `0x82:0xC` 的前置步骤；若失败，UMD 可能中止 TA 提交

## 2. Linux 侧现状盘点（实测：读源码）

### 2.1 SYNC 组 dispatch（`mt_pvr_bridge.c:4786`）

```c
case MT_PVR_BRIDGE_SYNC:
    switch (function) {
    case MT_PVR_FN_ALLOCSYNCPRIMITIVEBLOCK:  /* 0x0 */
        return pvr_cmd_sync_block(file, cmd);
    case MT_PVR_FN_FREESYNCPRIMITIVEBLOCK:   /* 0x1 */
        return pvr_stub_ok(cmd);
    case MT_PVR_FN_SYNCPRIMSET:              /* 0x2 */
    case MT_PVR_FN_SYNCALLOCEVENT:           /* 0x7 */
    case MT_PVR_FN_SYNCFREEEVENT:            /* 0x8 */
        return pvr_stub_ok(cmd);
    case MT_PVR_FN_SYNCPRIMCPUSIGNAL:        /* 0xA */
        return pvr_cmd_syncprim_set(file, cmd);
    default:
        return -ENOTTY;  /* ← SYNC:0xC 落在这里 */
    }
```

### 2.2 Function ID 定义（`mt_pvr_wire.h:480-485`）

```c
#define MT_PVR_FN_ALLOCSYNCPRIMITIVEBLOCK 0x0U
#define MT_PVR_FN_FREESYNCPRIMITIVEBLOCK  0x1U
#define MT_PVR_FN_SYNCPRIMSET             0x2U
#define MT_PVR_FN_SYNCPRIMCPUSIGNAL       0xaU
#define MT_PVR_FN_SYNCALLOCEVENT          0x7U
#define MT_PVR_FN_SYNCFREEEVENT           0x8U
/* 无 0xC 定义 */
```

**[实测]**：`grep` 确认无 `0xC` 的 SYNC function 定义；dispatch `default` 返回 `-ENOTTY`

### 2.3 已实现的 SYNC 命令

**AllocSyncPrimitiveBlock**（`pvr_cmd_sync_block`，`:3169`）：
- 创建 `MT_PVR_KIND_SYNC` 对象（`pvr_object_new`）
- 分配 PMR（`pvr_pmr_new(file, MT_PVR_SYNC_BLOCK_BYTES, 12)`）
- 返回 `{sync_handle, sync_pmr, block_size, vaddr}`（vaddr 为 CPU 映射，代 GPU VA）
- **[实测]**：r222 验证写回

**SyncPrimCpuSignal**（`pvr_cmd_syncprim_set`，`:1800`）：
- IN：`{sync handle, index, value}`
- 经 `pvr_translator_resolve` 解析 sync → 写 u32 到 PMR
- **[实测]**：r222 真实写入

### 2.4 Fence 机制（`mt_marker_fence.h`）

Linux 侧使用 `dma_fence`（内核标准 fence）：
- `struct mt_marker_fence`：含 `dma_fence`、`wire_id`、`mt_work_job`
- `mt_marker_ops`：6 个 op（tqx/context/submit/submit_work/ta_work/3d_work）
- 完成时 `dma_fence_signal()`，等待时 `dma_fence_wait()`

**Sync prim 与 Fence 的关系**：
| 层面 | 对象 | 作用 |
|---|---|---|
| Firmware/GPU | Sync prim（UFO） | GPU 侧同步计数器，firmware 读写 |
| Kernel | `dma_fence` | Linux 内核 fence 抽象，驱动内同步 |
| Bridge | `MT_PVR_KIND_SYNC` | Sync prim 的 PMR 块，用户态可见 |

桥侧在两者之间翻译：`0x82:0xC` 的 IN 含 sync prim 块句柄（`phClientTAUpdateSyncPrimBlock` 等），OUT 的 `update_fence` 回填 wire_id；`mt_marker_fence` 用 `dma_fence` 跟踪完成。

**[实测]**：源码结构；**[推断]**：翻译语义（需活体验证）

## 3. 缺口分析

### 3.1 缺失项清单

| # | 缺口 | 位置 | 性质 |
|---|---|---|---|
| R7-1 | `MT_PVR_FN_SYNCPROMIMPORTFD` (0xC) 常量未定义 | `mt_pvr_wire.h` | wire 定义缺失 |
| R7-2 | SYNC:0xC dispatch 无 handler | `mt_pvr_bridge.c:4786` | 走 `-ENOTTY` |
| R7-3 | IN/OUT 结构未入库 | `mt_pvr_wire.h` | 需从 5.2 头或 UMD 反推（IN 24B 已知，OUT 8B 未知布局） |
| R7-4 | FD→sync prim 的导入语义 | 待设计 | 需确定：FD 类型（dma-buf? sync_file? PVR 私有?）、导入后对象模型 |

### 3.2 与 Windows 的对比

**[推断]**（无 Windows KMD 源码，仅基于 UMD 行为）：
- Windows DDK2 的 `BridgeSyncPrimImportFD` 应已实现（UMD 调用它）
- Linux 侧缺失，UMD 在 Linux 上调用将得 `-ENOTTY`
- UMD 对 `-ENOTTY` 的处理未知：可能中止，可能降级（需活体验证）

### 3.3 是否需要 R1 完成？

r355 说"需 R1 完成后验证"。R1（UMD 真实建连）已在 r358 完成。但 R7 的验证还需要：
1. 真实 UMD TA 路径能推进到 `ZeusSyncPrimImportFD`（r361 被 `SyncPrimRef` 阻塞）
2. 观察 UMD 对 `-ENOTTY` 的反应

**[待验证]**：真实 UMD 是否调用 `ZeusSyncPrimImportFD`；调用失败时 UMD 行为

## 4. 与 R6 的关系

| 维度 | R6（Context） | R7（Sync Prim） |
|---|---|---|
| 对象 | `MT_PVR_KIND_CONTEXT`（render context） | `MT_PVR_KIND_SYNC`（sync prim block） |
| 缺口 | 无 firmware 状态（11 BO + 执行上下文） | SYNC:0xC 未实现（FD 导入） |
| 依赖 | 无 | 无（独立） |
| 阻塞场景 | 真实 TA/3D 渲染（firmware 需 context BO） | 真实 UMD TA（若 UMD 调用 ImportFD） |

**结论**：R6 与 R7 **相互独立**，无依赖关系。但真实 UMD 渲染**同时需要**两者：
- R6：firmware 执行需要 context 的 BO 状态
- R7：UMD 同步需要 sync prim 的 FD 共享

**[推断]**：基于架构分析；**[待验证]**：真实 UMD 的实际调用序列

## 5. 是否阻塞真实 UMD：分层结论

| 场景 | 是否阻塞 | 依据 |
|---|---|---|
| TA marker（r372/r373 已通） | 否 | 不经过 UMD，不调用 `ZeusSyncPrimImportFD` |
| UMD TA 路径（fabricated） | 未知 | r361 在 `SyncPrimRef` 被阻塞，未到达 ImportFD |
| 真实 UMD TA 调用 | **很可能** | 路径含 `ZeusSyncPrimImportFD → 0x82:0xC`；`-ENOTTY` 可能致中止 **[推断]** |
| 真实 UMD 3D 调用 | 未知 | 3D 路径的 sync prim 使用待调研 |
| 跨进程同步（如 Wayland 合成器） | **是** | FD 导入是跨进程共享 sync prim 的标准机制；缺失则无法共享 **[推断]** |

**一句话**：R7 不阻塞 marker 级验证；若真实 UMD 的 TA 路径调用 `ZeusSyncPrimImportFD`，则**阻塞**（`-ENOTTY`）。

## 6. 标注

- **[实测]**：SYNC dispatch 表（`mt_pvr_bridge.c:4786`）；Function ID 定义（`mt_pvr_wire.h:480-485`）；`ZeusSyncPrimImportFD` 反汇编（decompiled.c:51849）；`FUN_00139990` 的 bridge 调用（group=2, func=0xc）；`pvr_cmd_sync_block`/`pvr_cmd_syncprim_set` 实现；r361 的路径描述；r354 的调用链
- **[推断]**：R7 阻塞真实 UMD TA（基于路径分析，缺活体证据）；R6/R7 独立（基于对象模型分析）；跨进程阻塞（基于 FD 语义常识）；Windows 已实现（基于 UMD 调用行为）
- **[待验证]**：真实 UMD 是否调用 `ZeusSyncPrimImportFD`；UMD 对 `-ENOTTY` 的反应；IN 24B/OUT 8B 的详细布局；FD 的具体类型

## 门禁

- 本轮纯调研、无代码改动：`make -C mt-vgpu-guest check-offline` 全绿（见下）。
- 引用路径核对：`kernel/recovery/mt_pvr_bridge.c`（4786/3169/1800）、`kernel/mt_pvr_wire.h`（480-485）、`decompiled/linux-legacy-umd-5.2.0/decompiled.c`（51849/12416）均存在。

## 证据

- `mt-vgpu-guest/reports/r385-evidence.txt`：关键源码行摘录（dispatch 表、Function ID、UMD 反汇编）。
