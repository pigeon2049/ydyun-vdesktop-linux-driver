# r386：SyncPrimImportFD（SYNC:0xC）实现落地——FD 导入语义验证+返回当前值（离线，零硬件触碰）

## 结论

**SYNC:0xC `BridgeSyncPrimImportFD` 在 Linux 桥侧实现落地**（r385 缺口 R7-1~R7-4 关闭）：
- `mt_pvr_wire.h`：`MT_PVR_FN_SYNCPRIMIMPORTFD 0xcU` 常量 + 24B IN / 12B OUT 结构（KMD 5.2.0 头权威定义，非推断）
- `mt_pvr_bridge.c`：`pvr_cmd_syncprim_importfd()` handler + dispatch 路由
- Handler 做三件事：① `pvr_translator_resolve` 验证 hSyncBlock（-EOPNOTSUPP/-ERANGE）；② `fdget`/`fd_empty` 验证 FD（-EBADF）；③ 返回 offset 处当前 u32 值（零扩展为 u64），eError=0
- **FD payload 解释 TO-VALIDATE**：sync_file/dma-buf/PVR 私有三选一需真实 ExportFD 生产者 + 活体 UMD 确认；当前仅 log 不解释

**门禁**：438 Python + 299 C 全绿；`make kernel` W=1 零警告；反向验证通过（破坏 dispatch → 测试失败）。

## 1. Wire 定义（R7-1, R7-3）

### 1.1 来源：KMD 5.2.0 生成头（权威，非推断）

`reference/kmd-5.2.0-server-generated/common_sync_bridge.h:71`：
```c
#define MTGPU_BRIDGE_SYNC_SYNCPRIMIMPORTFD  MTGPU_BRIDGE_SYNC_CMD_FIRST+12
```

`:243/252` 结构定义：
```c
typedef struct MTGPU_BRIDGE_IN_SYNCPRIMIMPORTFD_TAG {
    MT_UINT32 ui32Fd;        /* offset 0 */
    MT_HANDLE hSyncBlock;    /* offset 4 */
    MT_UINT32 ui32Offset;    /* offset 12 */
    MT_HANDLE hDevmemCtx;    /* offset 16 */
} __packed MTGPU_BRIDGE_IN_SYNCPRIMIMPORTFD;  /* 24B */

typedef struct MTGPU_BRIDGE_OUT_SYNCPRIMIMPORTFD_TAG {
    MT_UINT64 ui64Value;     /* offset 0 */
    MTGPU_ERROR eError;      /* offset 8 */
} __packed MTGPU_BRIDGE_OUT_SYNCPRIMIMPORTFD;  /* 12B */
```

### 1.2 UMD 侧确认（r385 已发现，r386 复核）

`decompiled.c:12418` `FUN_00139990`：
```c
local_40 = param_5;  /* ui32Fd @ offset 0 */
local_3c = param_3;  /* hSyncBlock @ offset 4 */
local_34 = param_4;  /* ui32Offset @ offset 12 */
local_30 = param_2;  /* hDevmemCtx @ offset 16 */
iVar1 = FUN_00192930(param_1, 2, 0xc, &local_40, 0x18, &local_4c);
/* group=2, func=0xc, IN 24B (0x18), OUT=&local_4c */
```
**[实测]**：IN 布局与 KMD 头完全一致；OUT 12B（r385 报告的"8B"为 `local_4c` 栈槽观察近似值，KMD 头 12B 为准）。

### 1.3 Linux 侧入库（`kernel/mt_pvr_wire.h`）

```c
#define MT_PVR_FN_SYNCPRIMIMPORTFD 0xcU

struct MT_PVR_PACKED mt_pvr_syncprimimportfd_in {
    u32 fd;         /* MT_UINT32 ui32Fd */
    u64 sync_block; /* MT_HANDLE hSyncBlock */
    u32 offset;     /* MT_UINT32 ui32Offset */
    u64 devmem_ctx; /* MT_HANDLE hDevmemCtx */
};

struct MT_PVR_PACKED mt_pvr_syncprimimportfd_out {
    u64 value;      /* MT_UINT64 ui64Value */
    u32 error;      /* MTGPU_ERROR eError */
};

static_assert(sizeof(struct mt_pvr_syncprimimportfd_in) == 24, "0x2:0xc in");
static_assert(sizeof(struct mt_pvr_syncprimimportfd_out) == 12, "0x2:0xc out");
```
**[实测]**：KMD 头定义；MT_HANDLE 在 LP64 为 u64。

## 2. Dispatch Handler（R7-2）

### 2.1 路由（`kernel/recovery/mt_pvr_bridge.c:4857`）

```c
case MT_PVR_FN_SYNCPRIMIMPORTFD:  /* SyncPrimImportFD (r386) */
    return pvr_cmd_syncprim_importfd(file, cmd);
```

### 2.2 Handler（`:1850`）

```c
static int pvr_cmd_syncprim_importfd(struct mt_pvr_file *file,
                                     struct mt_pvr_cmd *cmd)
{
    struct mt_pvr_syncprimimportfd_in in;
    struct mt_pvr_syncprimimportfd_out out = { 0 };
    struct mt_pvr_ufo_cond cond;
    struct fd f;
    u32 val;
    int ret;

    ret = pvr_in(cmd, &in, sizeof(in));
    if (ret)
        return ret;
    ret = pvr_translator_resolve(file, in.sync_block, in.offset, &cond);
    if (ret)
        return ret;  /* -EOPNOTSUPP (非 SYNC 对象) / -ERANGE (越界) */
    f = fdget(in.fd);
    if (fd_empty(f))
        return -EBADF;
    pr_info("mt_pvr_bridge: syncprimimportfd: fd=%u sync=%#llx off=%u "
            "devmemctx=%#llx (FD payload TO-VALIDATE)\n", ...);
    fdput(f);
    memcpy(&val, (u8 *)cond.host + cond.offset, sizeof(val));
    out.value = val;
    out.error = 0;  /* MTGPU_OK */
    return pvr_out(cmd, &out, sizeof(out));
}
```

**设计决策**（诚实标注）：
- **[实测]**：hSyncBlock 验证复用 `pvr_translator_resolve`（与 `pvr_cmd_syncprim_set` 相同路径）；offset 越界由 `pvr_translator_pmr_cond` 保证 `-ERANGE`
- **[实测]**：FD 存在性用 `fdget`+`fd_empty` 验证（kernel 6.12 `struct fd` 为 opaque，`fd_file()` 宏取值）；无效 FD 返 `-EBADF`
- **[TO-VALIDATE]**：FD payload 不解释——sync_file？dma-buf？PVR 私有 export？需真实 `SyncPrimExportFD` (0x2:0xb) 生产者 + 活体 UMD 确认。当前 log 记录请求，不传输 fence 值
- **[推断]**：返回当前值（而非 0）是最有用的最小语义——UMD 读到的是 sync prim 的真实状态；若未来实现完整导入，值会被 FD 的 fence 值覆盖

## 3. FD 导入语义设计（R7-4）

### 3.1 Export/Import 配对（KMD 头）

| 方向 | 命令 | IN | OUT |
|---|---|---|---|
| Export | 0x2:0xb SyncPrimExportFD | {hSyncBlock 8B, ui32Offset 4B, ui64Value 8B} = 20B | {ui32Fd 4B, eError 4B} = 8B |
| Import | 0x2:0xc SyncPrimImportFD | {ui32Fd 4B, hSyncBlock 8B, ui32Offset 4B, hDevmemCtx 8B} = 24B | {ui64Value 8B, eError 4B} = 12B |

### 3.2 生命周期

```
进程 A (exporter)                进程 B (importer, UMD)
    |                                    |
    |-- 0x2:0xb ExportFD --------------->| (未实现)
    |   IN {sync, off, val}              |
    |   OUT {fd}                         |
    |                                    |
    |   fd via SCM_RIGHTS / 继承         |
    |===================================>|
    |                                    |
    |                    0x2:0xc ImportFD (r386 实现)
    |                    IN {fd, sync, off, ctx}
    |                    OUT {value, error}
```

### 3.3 与现有 sync prim 的关系

- `MT_PVR_KIND_SYNC` 对象（0x2:0x0 创建）：sync prim 的 PMR 块，进程内可见
- ImportFD：将**跨进程**的 sync prim 导入到**本进程**的 sync block
- `hDevmemCtx`：目标 device memory context（多设备场景；单设备可忽略，当前实现不使用——log 记录）
- 导入后：本进程通过 `pvr_translator_resolve` 看到更新的值；firmware 侧 UFO 同步由 driver 负责（TO-VALIDATE：是否需要写回 PMR？当前实现只读不写）

### 3.4 待验证清单

1. FD 类型：sync_file？dma-buf？PVR 私有 `anon_inode`？（需 ExportFD 实现或真实 UMD 抓包）
2. 导入是否写值：当前只读；真实语义可能要求将 FD 的 fence 值写入目标 offset
3. `hDevmemCtx` 的作用：多 GPU 上下文？（单 vGPU 场景可能恒为特定值）
4. UMD 对 `-EBADF` / `-EOPNOTSUPP` 的反应（r385：真实 UMD 可能中止 TA 路径）

## 4. 门禁

- `make -C mt-vgpu-guest check-offline`：**438 Python + 299 C 全绿**（新增 10 Python 测试）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**（`fdget`/`fd_empty`/`fdput` 6.12 API 正确）
- 反向验证：dispatch 的 `MT_PVR_FN_SYNCPRIMIMPORTFD` 改为 `0x99` → `test_pvr_syncprimimportfd.Sy
...[truncated 1118 chars]