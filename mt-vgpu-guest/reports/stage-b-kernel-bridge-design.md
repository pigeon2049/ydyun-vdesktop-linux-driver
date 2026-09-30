# Stage B：最小 PVR 内核桥设计（评审稿）

日期：2026-09-30。作者：适配会话。状态：**待评审**（用户已在 bA14 选定「先做设计文档」）。
证据标注沿用 Stage A 惯例：T=动态 trace / S=静态反编译 / H=头文件。
机器可读需求表：`reports/stage-b-bridge-requirements.json`
（由 `python3 scripts/build-stage-b-bridge-requirements.py` 生成，可复现）。

---

## 1. 目标与成功判据

让**原厂 Linux legacy UMD**（`libsrv_um_MUSA.so.1.0.0`，sha256 `b3058c02…`）在我们自研的
内核驱动上跑通连接→内存→渲染上下文→提交的全链路，从而让 Mesa/GLES/Vulkan/合成器
有机会直接复用原厂用户态，而不必逆向 PSC 编译器。

| 级别 | 判据 | 现状（bA14） |
| --- | --- | --- |
| L0 | UMD 能 `PVRSRVConnect` 返回 0 | ✅ 离线达成（伪造 KMD） |
| L1 | devmem 上下文 + 11 堆 + render context 创建成功 | ✅ 离线达成 |
| L2 | `CreateSyncPrim` 成功（真 memType） | ✅ 离线达成（`0x100000000`） |
| L3 | ZS 缓冲 / render target / kick 打包成功 | ❌ 伪造撞墙（见 §6） |
| L4 | kick 提交被硬件消费，画面出现 | ❌ 未开始 |

**Stage B 的交付物 = L3 的内核实现**（L4 需要 L3 的命令包审通后再上硬件）。

---

## 2. 范围与边界

**做**：

1. 新增独立内核模块（recovery 目录，命名建议 `mt_pvr_bridge`），注册**自己的** DRM 设备。
2. 实现 Stage A 已验证的 19 条 bridge 命令 + sync ioctl 族。
3. PMR/映射落到既有 BO/VM/显存设施（复用 `mt_guest_device.address_spaces` 与 `mt_bo`）。
4. 事件/fence 接到既有 `drm_syncobj` + 固件完成事件链。

**不做**（本期）：

- 不动 `mt_guest_probe` 主模块的活会话，不重载主模块，不重置引擎。
- 不实现 L4（真实提交上硬件），但命令包必须可被离线审。
- 不追求 205 条 bridge 全实现；只做已验证路径 + 明确的第二波。

**回退**：`rmmod mt_pvr_bridge` 即可；主模块与既有 `mt_live_3d_drm` 不受影响
（新节点独立，version 名不同，不与现有 `mtvgpu` 节点冲突）。

---

## 3. 已确认的用户态 ABI（每条带证据）

### 3.1 节点与 ioctl 面

| 项 | 值 | 证据 |
| --- | --- | --- |
| DRM version name | 必须字面 `pvr` | T：改 shim 返回 `pvr` 后首个节点即中；S：`0xa4989` 的 `strcmp` |
| bridge 包 ioctl | `0xc0206440` | T：全部 bridge 走这一个 ioctl |
| bridge 包结构 | `{u32 bridge_id; u32 function_id; u64 in_ptr; u64 out_ptr; u32 in_size; u32 out_size;}`（32B） | T/H |
| INIT ioctl | `0x40046445`，`u32 init_module`：`1`=通用连接，`2`=device 连接 | T：第二次连接 `init_module=2` |
| sync ioctl 族 | `0x40206440`–`0x40206445`（`0x41`=RENAME 已观测） | T |
| 节点类型 | UMD 先扫 **render** 节点；device 连接另开 **card** 节点 | T：`renderD*` 与 `card0` 均被 open |
| 多次打开 | 必须支持同一进程多开节点（TA timeline 需要） | T：bA9/bA10 |

> 关键：DRM 的 `DRM_IOCTL_VERSION` 直接返回 `dev->driver->name`。
> 现有 recovery 模块 `.name = "mtvgpu"`，**Stage B 必须另起一个 `.name = "pvr"` 的设备**。

### 3.2 句柄与 mmap 偏移（本轮新确认，设计基石）

对 4 份 trace 的 **28 次 mmap** 全部成立（T，脚本核验）：

```
mmap_offset == local_handle << 12 ，且 offset 低 12 位恒为 0
```

即 UMD 把 bridge 返回的**本地句柄**当 DRM mmap 偏移用（`handle << 12`）。
因此内核侧必须保证：凡返回给 UMD 的可映射句柄 `h`，`h << 12` 必须是该 PMR 的
合法 DRM mmap 偏移。实现上就是「每个可映射 PMR 分配一个 4KiB 对齐的 VMA offset，
返回 `offset >> 12` 作为句柄」。

### 3.3 Connect（`0x1:0x0`）

- IN 16B：`ui32ClientBuildOptions=0x80000850`、`ui32ClientDDKBuild`、
  `ui32ClientDDKVersion=0x10000`、`ui32Flags`。
- OUT 17B：`ui64PackedBvnc@0` + `eError@8` + `ui32CapabilityFlags@12` + `ui8KernelArch@16`
  （H，与 UMD 实测 17 字节一致；bA14 初稿曾误记头文件为 16，已由单测纠正）。
- `Bvnc` 必须在 allow-list：`0x0001000000000000` 或 `0x0023000406600017`（S，`0x3b8c5/0x3b8d8`）。
  未命中时 UMD 覆写为后者并继续（S），所以**上报后者即可**。
- 紧随其后的三道子门：`top16==0x23`、`bits[31:16]==0x660`、`C==0x17`、`V==4`
  ——由上面这一个 u64 一次性满足。

### 3.4 info 页

- `0x1:0xf ACQUIREINFOPAGE`（OUT 12B：`hPMR` + `eError`）→ `0x6:0x6 PMRLOCALIMPORTPMR`
  （IN 8B hPMR / OUT 28B：align、size、本地句柄）→ `mmap(句柄<<12)`。
- 页内容（S + T）：`+0x00` 设备数=1；`+0x44` KMD 能力位必须覆盖 `0xb57`；
  `+0x48` KMD build 魔数 `0x688a847`（忽略 bit16）。
- 尺寸：mmap 长度 `0x10000`（64KiB）。

### 3.5 堆表

- `0x6:0x1e HEAPCFGHEAPCOUNT` → 11；`0x6:0x20 HEAPCFGHEAPDETAILS`（IN 20B / OUT 44B）逐 index。
- IN：`puiHeapNameOut` 指针、`ui32HeapConfigIndex`、`ui32HeapIndex`、`ui32HeapNameBufSz`（H）。
  名字由 KMD 拷进调用者缓冲（BufSz=160，bA5 修正过一次漏写）。
- OUT：base、length、reserved、name 指针、eError、`Log2DataPageSize`、`Log2ImportAlignment`。
- 名字表（UMD 按名查找，`FUN_00194a60`）：`General`、`PDS Code and Data`、`USC Code`、
  `Component Control`（其余为 NULL，UMD 会在下一次错误里报出它想要的名字）。
- 堆几何**以 `kernel/mt_guest_heaps.h:mt_guest_plan_heaps()` 为准**（11 项，
  含两个 base=0/size=0 的空堆）。注意 shim 里那张表是早期近似
  （heap4/7/8 尺寸不同），不要照抄。
- **数值已闭环（bA16）**：Windows `mtkm64.sys` 的 22 项 GPU-VA 堆表
  （`FUN_14001ccd8`，MMU 模式 0 选表 `0x141030d90`）中，我们填的 9 个非空堆
  **逐字节一致**，4/5 两堆双方都空；`scripts/dump-windows-heap-table.py` 可复现，
  `tests/test_windows_heap_table.py` 门禁。见 `reports/windows-kmd-crosscheck.md` §1。
- **名字只能来自 PVR UMD**：Windows 侧只给 13 项**资源**命名
  （`Free List / Pmva Info / PMVA / Paging Command / Static PDS / Dynamic PDS /
  Fence GPU Memory / Paging Ctx Buffer / PH1 patch buf / Static USC /
  YUV Coefficient / DM Kill / Texture state`），从不给堆命名。

### 3.6 PMR 生命周期（devmem 阶段实测顺序）

```
0x6:0x9  PHYSMEMNEWRAMBACKEDPMR   IN 72 / OUT 24   (hPMR, flags, isSystemMem…)
0x6:0x15 DEVMEMINTRESERVERANGE    IN 24 / OUT 12
0x6:0x13 DEVMEMINTMAPPMR          IN 32 / OUT 12
0x6:0x7  PMRUNREFPMR / 0x6:0x3 MAKE_LOCAL_IMPORT_HANDLE
```

- 所有返回句柄必须**互异**；全零或别名会让 UMD 走不同死法（bA4）。
- `uiOutFlags` 需回 `0x1233` 级别的合理位、`isSystemMem=1`（T，仅 RAM 后备 PMR）。
- 内核侧：PMR → `mt_bo`（系统内存后备）；`MAPPMR` → 在该 device 连接私有的
  GPU VM 空间里 `bind`；`RESERVERANGE` → 预留 VA 区间不落页。

### 3.7 Sync

| 命令 | 方向 | 要点 |
| --- | --- | --- |
| `0x2:0x0 ALLOCSYNCPRIMITIVEBLOCK` | IN 8 / OUT 32 | **memType = `0x100000000`**（bA13 实测）；OUT 必须句柄/PMR/BlockSize/VAddr 全非零，否则 UMD 自己的 RA 二次分配报 `0x652` 拒零 |
| `0x2:0x7 SYNCALLOCEVENT` | IN 20 / OUT 4 | bA13 新观测；IN 结构待对头文件精读 |
| `0x2:0x1 FREESYNCPRIMITIVEBLOCK` | IN 8 / OUT 4 | 释放对称 |

`BlockSize` 用 `0x1000` 可过（4KiB 对齐的最小块）。

### 3.8 双连接

- 通用连接：`PVRSRVConnect(&conn, flags)` → `INIT(1)` + 完整 Connect 序列。
- device 连接：`PVRSRVConnectionCreateDevice(&conn, b7, 0)` → **第二个节点 + `INIT(2)`**，
  在其上重做 devmem + render context。**渲染路径必须走 device 连接**（bA10）。
- 两连接的堆/PMR/事件句柄空间必须相互独立（实测两连接互不干扰）。

---

## 4. 阶段一：19 条命令的实现规格

由 `reports/stage-b-bridge-requirements.json` 生成（含每条 IN/OUT 字段偏移）。
下表为「已观测」命令；同一 JSON 里还有 205 条全量（多数属于别的子系统）。

| bridge | 命令 | IN | OUT | 内核侧实现要点 | 复用到 |
| --- | --- | --- | --- | --- | --- |
| `0x1:0x0` | CONNECT | 16 | 17 | 见 §3.3；Bvnc 上报 `0x0023000406600017` | — |
| `0x1:0x1` | DISCONNECT | 0 | 4 | 拆本连接的服务/事件/PMR | — |
| `0x1:0x2` | ACQUIREGLOBALEVENTOBJECT | 0 | 12 | 返回非零 OS 事件句柄（UMD 会真消费） | `mt_fw_event*` |
| `0x1:0x3` | RELEASEGLOBALEVENTOBJECT | 8 | 4 | 对称释放 | |
| `0x1:0x4` | EVENTOBJECTOPEN | 8 | 12 | OS 事件 fd 导出，供用户态 poll | `drm_syncobj` |
| `0x1:0x5` | EVENTOBJECTWAIT | 8 | 4 | **阻塞到完成事件**；与 fence 链打通 | 固件完成事件 |
| `0x1:0x6` | EVENTOBJECTCLOSE | 8 | 4 | | |
| `0x1:0xd` | EVENTOBJECTWAITTIMEOUT | 16 | 4 | 超时版本 | |
| `0x1:0xa` | ALIGNMENTCHECK | 12 | 4 | 返回 0 即过 | — |
| `0x1:0xc` | GETMULTICOREINFO | 12 | 16 | 回 `numCores`（本机 1 核） | `mt_guest_state` |
| `0x1:0xf` | ACQUIREINFOPAGE | 0 | 12 | 返回 info 页 PMR 句柄 | §3.4 |
| `0x1:0x10` | RELEASEINFOPAGE | 8 | 4 | | |
| `0x6:0x3` | PMRMAKELOCALIMPORTHANDLE | 8 | 12 | 产出可 mmap 的本地句柄 | §3.2 |
| `0x6:0x6` | PMRLOCALIMPORTPMR | 8 | 28 | align/size/本地句柄 | |
| `0x6:0x7` | PMRUNREFPMR | 8 | 4 | 引用计数 | `mt_bo` refs |
| `0x6:0x9` | PHYSMEMNEWRAMBACKEDPMR | 72 | 24 | 系统内存后备 PMR | `mt_bo_vram` / system pages |
| `0x6:0xf` | DEVMEMINTCTXCREATE | 4 | 24 | 非零 ctx 句柄 + 64B cacheline | 连接私有对象 |
| `0x6:0x10` | DEVMEMINTCTXDESTROY | 8 | 4 | | |
| `0x6:0x11` | DEVMEMINTHEAPCREATE | 28 | 12 | 句柄按 base 互异 | §3.5 |
| `0x6:0x12` | DEVMEMINTHEAPDESTROY | 8 | 4 | | |
| `0x6:0x13` | DEVMEMINTMAPPMR | 32 | 12 | 在连接 VM 里 bind + 上页表 | `address_spaces.ops->bind/upload` |
| `0x6:0x15` | DEVMEMINTRESERVERANGE | 24 | 12 | 预留 VA 不落页 | `ops->bind`（no-page） |
| `0x6:0x1e` | HEAPCFGHEAPCOUNT | 4 | 8 | 11 | `mt_guest_heaps.h` |
| `0x6:0x20` | HEAPCFGHEAPDETAILS | 20 | 44 | 名字回写调用者缓冲 | §3.5 |
| `0x2:0x0` | ALLOCSYNCPRIMITIVEBLOCK | 8 | 32 | memType `0x100000000` | §3.7 |
| `0x2:0x7` | SYNCALLOCEVENT | 20 | 4 | | |
| `0x82:0x8` | RGXCREATERENDERCONTEXT | 60 | 12 | 返回非零 render ctx 句柄；内部会再申请 12×PMR/保留/映射 | §3.6 |
| `0x86:0x4` | RGXACQUIREHWPERFFSETTINGS | 0 | 12 | 输出全零即可过 | |

> 表中 0x6:0x7/0x10/0x12 与 0x1:0x1/0x3/0xd 属「成对出现但当前会话未触发」，
> 仍需实现——真实 KMD 上 UMD 走 teardown 路径会调用。

### 4.1 ABI 差量（UMD 实发 > 5.2 头声明）

单测 `test_umd_wire_sizes_exceed_some_header_structs` 钉住的三条，说明
**内核必须按 UMD 的尺寸接收**，多出的尾部字节不能丢：

| 命令 | 方向 | UMD 实发 | 5.2 头 | 差 |
| --- | --- | --- | --- | --- |
| `0x6:0x9 PHYSMEMNEWRAMBACKEDPMR` | IN | 72 | 68 | +4 |
| `0x6:0x9 PHYSMEMNEWRAMBACKEDPMR` | OUT | 24 | 20 | +4 |
| `0x6:0x13 DEVMEMINTMAPPMR` | IN | 32 | 28 | +4 |

另有 `0x1:0x2`/`0x1:0xf` 的 IN：头声明 4 字节，UMD 传 0（无输入缓冲）——按 0 处理。

---

## 5. 阶段二：kick / 提交族（第二波，规格先行）

以下命令的 IN/OUT 大小来自 205 全量审计（`legacy-umd-pvr-bridge-abi.json`），
**尚未在离线会话中跑过**，实现时以实测为准：

| bridge | 命令 | IN | OUT | 说明 |
| --- | --- | --- | --- | --- |
| `0x82:0x2` | RGXCREATEZSBUFFER | 24 | 12 | bA14 卡在这条（见 §6） |
| `0x82:0x3` | RGXDESTROYZSBUFFER | 8 | 4 | |
| `0x82:0x4` | RGXPOPULATEZSBUFFER | 8 | 12 | |
| `0x82:0x5` | RGXUNPOPULATEZSBUFFER | 8 | 4 | |
| `0x88:0x0` | RGXCREATEKICKSYNCCONTEXT | 16 | 12 | `RGXCreateKickSyncContextCCB(conn, devmemctx, …)` |
| `0x88:0x1` | RGXDESTROYKICKSYNCCONTEXT | 8 | 4 | |
| `0x88:0x2` | RGXKICKSYNC2 | 56 | 8 | |
| `0x88:0x3` | RGXSETKICKSYNCCONTEXTPROPERTY | 20 | 12 | |
| `0x88:0x4` | RGXKICKSYNC | 84 | 8 | 真正的 kick 参数包入口 |
| `0x88:0x5` | RGXKICKSYNC | 8 | 12 | |
| `0x88:0x6` | RGXKICKSYNC | 8 | 4 | |
| `0x88:0x7` | RGXKICKSYNC | 68 | 4 | |
| `0x82:0xc` | RGXKICKTA3D2 | 268 | 12 | |
| `0x82:0xe` | RGXKICKTA3D3 | 276 | 4 | UMD 走这条（审计覆盖内） |
| `0x82:0x10` | RGXKICKTA3D4 | 268 | 12 | |
| `0x82:0xf` | CREATE_FENCE | 44 | 8 | |
| `0x81:0x5` | RGXKICKCDM2 | 108 | 8 | 计算路径 |
| `0x8c:0x04` | SUBMITTRANSFER | 108 | 8 | 传输引擎 |

**顺序建议**：ZS 缓冲 → render target（`RGXAddRenderTarget`，纯 UMD 侧对象图）→
KickSyncContext → 用 `CreateSyncPrim` 产物 + DebugPrintf/`0x652` 定位法整形 kickTA
→ 进 `RGXPrepareTA`（bA14 已把崩点推进到这里）。

---

## 6. 为什么必须真 KMD（离线撞墙的两处证据）

1. **renderctx / kick 事件过滤器**（bA12→bA13 勘误）：并非对象图缺失，
   而是 harness 传参与 `DevmemAllocateExportable` 内部校验（size/align/flags/
   Log2AllocSizePage 组合）无法用「清零 + 猜几个字段」满足。
2. **ZS 缓冲**（bA14）：参数序修正后进入 `FUN_00195660`，在其内部校验返回
   `MTSRV_ERROR_INVALID_PARAMS`（`0x6e3` dprintf 定位）。该层依赖真实堆几何，
   伪造不再收敛。

共同点：UMD 内部对**分配几何**做了多字段交叉校验，零值/猜测值必然被拒。
这正是 Stage B 存在的理由——把「真值」换成内核里的真分配。

---

## 7. 内核侧模块设计

### 7.1 模块与节点

- 新文件 `kernel/recovery/mt_pvr_bridge.c`，`W=1` 零警告为门禁。
- `static const struct drm_driver`：**`.name = "pvr"`**、`.driver_features =
  DRIVER_RENDER | DRIVER_GEM | DRIVER_SYNCOBJ`（render 节点优先被 UMD 扫到）。
- 打开时按 `mt_live_3d_drm` 的 `allowed()` 模式校验 `00:0e.0` 归本驱动所有，
  否则拒绝——不与主模块抢设备。
- 节点与主模块/既有 recovery 节点并存，卸载只销毁自己的对象。

### 7.2 状态模型

```c
struct mt_pvr_file {          /* 每个 open */
        struct mt_pvr_conn   *conn;      /* 通用连接或 device 连接 */
        ...
};
struct mt_pvr_conn {          /* 一个 PVRSRV 连接 */
        u64                   srv_handle;      /* GetSrvHandle() 用 */
        u32                   init_module;     /* 1 通用 / 2 device */
        struct mt_gem_store   *pmr_store;      /* PMR 表（句柄互异） */
        struct mt_gpu_vm      *vm;             /* 连接私有 GPU VA 空间 */
        struct list_head      heaps[11];       /* 按 index 的堆对象 */
        struct mt_event       *global_event;   /* 0x1:0x2 句柄 */
        struct drm_syncobj    *syncobjs;       /* 0x1:0x4 导出的 fd */
};
```

连接对象尺寸必须 ≥ UMD 期望（关键字段：`+0x14` flags、`+0x28` info 页指针、
`+0x48` TL 流、`+0x50` HWPerfUm、`+0x60` HWPerf 设置、`+0x68/+0x70/+0x78` 互斥/计数/devctx、
`+0xa0` 特征块、`+0xb0`×2 sync arena）——这些偏移已由 bA13 实测确认。

### 7.3 句柄与偏移

- 全局单调句柄分配器：`PMR/event/heap/ctx` 各有独立计数器，**保证互异**。
- 可映射 PMR：`drm_gem` 对象 + `drm_vma_offset_manager` 分配 4KiB 对齐 offset，
  bridge 返回 `offset >> 12`（§3.2）。
- `RESERVERANGE` 只做 VA 区间预留（`ops->bind` 但不 `upload`），
  保证不超页预算（r42 起页预算 64MiB / 15872 项是硬边界）。

### 7.4 事件与 fence（按 Windows 原厂模型，bA16 校核）

Windows KMD 的完成模型经反编译核实如下，可直接照搬（证据见
`reports/windows-kmd-crosscheck.md` §2）：

- **令牌不是驱动自增的**：FenceID 由 OS 分配，随提交命令下到驱动；
  驱动把它写进工作记录，固件完成时**原样回填**到完成事件，驱动只做**比对**。
- **每队列 0x40 字节队列块**：`+0x04` lastPrepared / `+0x08` lastCompleted /
  `+0x0c` lastFenceAtDPC / `+0x14` 提交 tail / `+0x20` 记录环基址 /
  `+0x28` 完成 head / `+0x2c` 提交 tail / `+0x48` DM 号。
- **提交记录 0x98 字节**：`+0x04` flags（`0x20`=可抢占）/`+0x08` 令牌/
  `+0x10` context/`+0x28..0x30` 哨兵 `0xffffffff`。
- **固件环**：每 DM 块 `0x2e30`；命令环 64×`0x50`；完成事件 64×`0x18`
  （`+0x04` 类型、`+0x08` 令牌）；head/tail 各一对，索引 `& 0x3f`，单生产者单消费者。

对应到我们：

| 组件 | 我们的实现 | 备注 |
| --- | --- | --- |
| 令牌 | 每（device 连接, 队列）单调 `u64` | 不需要 OS 分配，Stage B 自己发 |
| 提交环 | 每连接 64 槽定长记录（借鉴 0x98 布局） | 满时回 `-EAGAIN`（厂商是自旋后丢弃，我们不学） |
| 完成环 | 复用 `kernel/mt_fw_event*` 的固件完成事件 | 就是厂商的「固件事件环」对应物 |
| `EVENTOBJECTWAIT` | 阻塞直到令牌被完成环兑现 | 对应 `d->address_spaces` 之外的独立等待队列 |
| `EVENTOBJECTOPEN` | `drm_syncobj_create` + 导出 fd | UMD 拿 fd 建 timeline |

- 唤醒路径：厂商**没有** per-fence 的内核事件，只在完成时回调一次
  （`DxgkCbNotifyInterrupt`，0x50 字节，Type 1/2/9）。我们对应
  `dma_fence_signal` + `drm_syncobj`，一次完成一次唤醒。
- 这条链决定了 L3→L4 的可行性：**没有它，UMD 会在第一次 kick 后挂死**。

### 7.5 共享 ABI 门禁

7 个共享结构（`mt_guest`/`mt_guest_device`/`mt_gpu_vm`/`mt_vm_binding`/
`mt_vm_vram`/`mt_vm_store`/`mt_work_job`）的 pahole 摘要基线在
`reports/shared-abi-baseline.json`；新模块必须通过
`scripts/verify-runtime-integration.py` 的 ABI 门禁。

---

## 8. 验证计划（分阶段，每阶段可单独回退）

| 阶段 | 内容 | 判据 | 回退 |
| --- | --- | --- | --- |
| S0 | 纯内核单测：句柄分配器、offset 编解码、堆表回填、ABI 门禁 | RAM 测试全绿，`W=1` 零警告 | 无需卸载 |
| S1 | 加载模块（**不绑定硬件**），用离线 UMD + shim 打真实 ioctl | 19 条命令全部返回 0，trace 与 bA13 逐条对齐 | `rmmod` |
| S2 | 绑 `00:0e.0`，跑到 `CreateSyncPrim` | 返回 0，memType 实测仍是 `0x100000000` | `rmmod`（不 seal） |
| S3 | 实现第二波，跑通 ZS/kick **只审包** | kick 包落盘，字段与 PSC 期望逐项对照 | `rmmod` |
| S4 | 真机提交（L4） | 需用户另行拍板（影响硬件状态） | 见 MEMORY 注意事项 |

**S0 的当前状态（bA17）**：第一部分已完成并接入门禁。

| 产物 | 作用 |
| --- | --- |
| `kernel/mt_pvr_wire.h` | 阶段一命令的线上结构（packed，逐字段断言；三处 wire 差量显式建模为保留尾部） |
| `kernel/mt_pvr_queue.h` | 令牌分配器 + 提交环/完成环（照 §7.4 厂商模型）+ mmap 偏移编解码 + 句柄分配器 |
| `tests/pvr_bridge_core_test.c` | 105 项 RAM 检查（ASan+UBSan）：令牌不匹配、陈旧事件、环满、回绕、fault 语义 |
| `tests/test_pvr_wire_sizes.py` | 4 项门禁：C 结构尺寸 ↔ `stage-b-bridge-requirements.json` 互为门禁 |
| `scripts/verify-runtime-integration.py` | 已注册新测试，随全量门禁一起跑 |

S0 剩余：堆表回填（把 `mt_guest_plan_heaps()` 接到 `0x6:0x20` 的 44 字节输出与堆名回写）。

**S1 的当前状态（bA18）**：模块已写好并通过 W=1 构建，**尚未加载**。

| 产物 | 说明 |
| --- | --- |
| `kernel/mt_pvr_device.h` | 连接对象布局（UMD 读取点逐偏移断言）、feature 块按偏移访问器、Connect 结果、info 页构造 |
| `kernel/recovery/mt_pvr_bridge.c` | DRM 节点 `.name = "pvr"`；两条 ioctl 号写死为 UMD 实际使用的 `0xc0206440` / `0x40046445`；19 条命令分发；PMR=系统内存、句柄=真实分配器、堆几何=计划表 |
| 安全边界（静态核验） | 模块内**无** `pci_register_driver`、无 `ioremap`/`readl`/`writel`、无 BAR 申请；不绑定 `00:0e.0`，主模块活会话不受影响 |

验收方式（待执行）：把 shim 切成「只记录不伪造」，让离线 UMD 打真实 ioctl，
trace 与 bA13 逐条对齐。**加载前需用户确认。**

**S1 的关键手法**：把 `probe/umd_bridge_shim.c` 切成「只记录、不伪造」模式，
让 UMD 打真实内核 ioctl——这既是验收手段，也是把离线成果搬到真桥的回归基线。

---

## 9. 风险

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 真 KMD 触发 UMD 此前未走过的错误路径（如 UMD 自身 double-free） | 崩溃 | ASan 复现过的错误路径要避开；必要时先只审包 |
| PMR offset 与句柄空间不匹配 | UMD mmap 失败即全链路断 | §3.2 已给硬约束，S1 用真实 ioctl 验证 |
| 堆几何与 UMD 期望不符（我们的 11 堆含空堆） | devmem 或 ZS 失败 | 以 `mt_guest_plan_heaps()` 为准，空堆显式 0 尺寸；必要时用 UMD 的错误消息反推名字 |
| 事件/fence 不通 | kick 后挂死 | S2 优先打通 wait 再做 kick |
| 与现有 `mtvgpu` 节点并存产生节点号顺延 | 既有工具硬编码路径失效 | 沿用 r44 做法：临时 symlink，用完即删 |

---

## 10. 未决问题（需实测或需外部信息）

1. 真机上 `HEAPCFGHEAPCOUNT` 到底报几个（我们计划报 11，含 2 个空堆）？
   —— bA16 已把**数值**与 Windows 22 项表闭环（9 个非空堆逐字节一致），
   但「向 UMD 报几个」这个协议值仍需真机确认。
2. `RGXCREATEZSBUFFER`（`0x82:0x2`，IN 24B）的 24 字节字段分解——
   Stage A 未跑通，需要真实 KMD 才有数据。
3. `SYNCALLOCEVENT`（`0x2:0x7`，IN 20B）字段分解（头文件可查，但与实际调用是否一致待验）。
4. `0x88:0x5/0x6/0x7` 三条 **任何可得头文件都没有**（2.7.1 的 RGXKICKSYNC 止于 `+4`，
   5.2 Host 包不含 RGX 组）——只能靠真机抓取或 5.2 Guest 侧头文件补齐。
5. `0x88:0x4 RGXKICKSYNC`（IN 84B）里哪些字段是命令包指针（需要真实映射才能读）。
6. UMD 是否会走 `PVRDRMGetRenderFromFD`（比较 `mtgpu`）那条路径——若是，需要第二个节点名。

---

## 11. 复现命令

```sh
cd mt-vgpu-guest

# 需求表（19 条已验证 + 205 全量，含字段偏移）
python3 scripts/build-stage-b-bridge-requirements.py

# 离线会话（connect → device-conn → devmem → renderctx → sync）
UMD=/tmp/mtt-linux-umd-5.2.0/root/usr/lib/x86_64-linux-gnu/libsrv_um_MUSA.so.1.0.0
UMD_TRACE=/tmp/opencode/umda/sess.jsonl \
LD_PRELOAD=$PWD/build/probe/umd_bridge_shim.so \
timeout 20 ./build/probe/umd_connect_harness $UMD \
  connect 0 buf 7 64 call PVRSRVConnectionCreateDevice b7 u0 u0 \
  buf 5 16 call RGXCreateDeviceMemContext b7* b5 b5+8 \
  buf 6 256 u64 6 16 'b5*+0' u32 6 48 u1 u32 6 52 u1 \
  buf 9 8 call RGXCreateRenderContext b7* b6 b9 \
  buf 14 8 u64 14 0 '*b7*+176' buf 10 64 buf 11 32 \
  call CreateSyncPrim 'b14*' b10 b11

# 既有全量离线验证（21 RAM 测试 + W=1 构建 + ABI 门禁）
python3 scripts/verify-runtime-integration.py
```

---

## 12. 相关文档

- `reports/bridge-stage-a-triage.md`：Stage A 全部证据（§20 为 bA13/bA14）。
- `reports/legacy-umd-pvr-bridge-abi.md`：205 条 bridge 的 ABI 审计。
- `reports/stage-b-bridge-requirements.json`：本文表格的机器可读版
  （205 条命令名 + IN/OUT 字段偏移 + 线格式差量 + 19 条已观测命令）。
- `tests/test_stage_b_bridge_requirements.py`：钉住需求表结构与 trace 不变式。
- `reference/kmd-5.2.0-server-generated/`：5.2.0 KMD 生成头（线格式权威参考，
  从 `/tmp` 复制留档；按仓库惯例不跟踪）。
- `MEMORY.md`：阶段索引与硬件注意事项。
