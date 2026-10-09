# S3000 vGPU 驱动适配 —— 阶段进度快照

**快照时间**：2026-10-08
**仓库**：`/opt/ydyun-vdesktop-linux-driver`（分支 main）
**对应提交**：`d8ed054`（内容基线：最后一次改动本文件快照内容的提交；
纯文档整理提交若未动本文件，不推进该指针，避免 amend 死循环）
**新 agent 入口**：先读仓库根 [`STATUS.md`](STATUS.md)，再读本文件对应章节。
**硬件**：Moore Threads S3000，PCI `1ed5:0222`，Debian 13，kernel `6.12.111+deb13-amd64`；Guest 配额 1GiB（`vm_memory_size_bytes`，r270；BAR2 窗口 16G、固件启动池 90MB 均非配额）

本文件是**当前状态的唯一权威快照**（细节层）。逐轮过程记录在根目录 `MEMORY.md`（只留最新两节）。
状态冲突时裁决顺序：`STATUS.md` → 本文件 → `MEMORY.md`。

---

## 1. 一句话现状

厂商 MASA UMD 已经能在我们的内核桥驱动上走完
`PVRSRVConnectionCreateDevice` → `RGXCreateDeviceMemContext` →
`RGXCreateRenderContext` → `CreateSyncPrim` →
`RGXCreateKickSyncContextCCB` → `RGXDestroyKickSyncContext` →
`RGXCreateComputeContext` → `RGXDestroyComputeContext` →
`RGXCreateZSBuffer` → `RGXKickSync`（经 `0x88:0x4` accept-and-inspect，
即时 fence），**全部返回 0，进程正常退出**。
compute/kick-sync context 只是对象生命周期管理，不涉及硬件提交；
真正的 kick 翻译（firmware 环重编）仍未开始，那是 S4 边界。
S4-3 交接第一步已落桥并在载：DMA 注册（PCI 直连）、VM plan
（arena + cover-page）、kick T1+T2 只读观察。
经 `live_3d`/`live_3d_drm` 路径的 RGX 真实执行已完成：
单帧 DM2（r66）→ 64 KiB 像素读回（r70）→ 20 帧批量零 fault（r71）。
r72–r114 进展：合成 kick 之外，非零 check 包已 fabricated 复现
并活体验证（`ufo_known=1/1`，r72/r73）；CCB 打包公式已得
（r76）；update 侧定位到 DDK2（需桥特性开关，r74–r78）；
TA 提交链打通到 `0x82:0x14c` 桥（r82–r84）；
Rogue2D/0x89 TDM 组已映射并实现共享内存桥（r86–r89，未加载）；
check-only 首帧翻译设计完成（r113）；T1/T2 输入侧链条闭环（r114）。
r115–r184 进展：DDK2 全链在 `drm_major=2` 下走通（render2/CCB2 建销，
r141–r146）；check-only kick 经真实 DM2 空 marker 完成（r147–r149，
legacy 与 DDK2）；真实 SubmitTransfer3 的 39B 非零 CCB 活体落定，
与 fabricated 逐字节一致（r172–r174）；扩展区算术闭合、T3 输入规约
v1、输出侧盘点、几何通道、fill 构造器、dry-run 活体验证通过
（r175–r181）；TQX bring-up 打通（r182，`-22` 系创建顺序）；
ref 漂移离线审计 + defaults 活体差分 Δ0（r183–r184，maps 无罪，
kill-while-busy 精炼假设待关账）。
r185–r279 进展：残留收敛（scene 预设/槽位/驱动名/DID 合一，r186–r189，
r271–r273）；update 路径定位到 `0x82:0x14`（r190）；GFX producer
recon（r192–r210：`MUSAKICKGFX5` 108B 对齐，0x4700 UMD 原始 CCB 捕获）；
会话两度重建（r211/r264，2/2 pinned）；`0x82:0x14` observer 落桥并全路径
活体验证（r215–r218，r224–r225）；TQX bring-up 新会话复验（r219）；
SyncPrimSet 真写 + translator 值语义闭环（r220–r223，r227/r231–r236）；
TQX slices bring-up（r261–r266，r263 死锁→r265 锁序修复）；fire 离线实现
（r267–r268）与 64 页绕行验证（r278–r279）。
r280–r316 进展：桥分块 fire（r280）→ 独立模块自有节点（r282）→ 首次真发射
全绿（r283：21 块/1310720 像素逐块验过）→ 通用性/soak/上限/拒绝全覆盖
（r284/r285/r287/r289）→ UMD 驱动 fire 首绿及复现/双发（r290–r292）→
hanging 定为 `SyncPrimWait` 用户态 spin + 100 秒有界自杀（r293–r296：
wchan/R/GDB/core/入口三元组）→ submit3 update 回写满足 UMD 越过 submit3
（r297–r299：`-95` 拒/NULL 跳过/`0x1029` 写 1）→ fire-into-destination
（r300–r301）→ 归属反转与 CCB 定向（r302–r304：官方树无执行逻辑可抄）→
magic 全零/VA 普查/pristine 规则（r305–r307）→ 颜色三杀（r308–r309）→
形态 solid/边界（r310–r313）→ 比对点名 `dest+0 vs source+3841`（r314）→
池基修正后 **`Test PASS (exit=0)`，真实绘制像素闭环**（r315–r316）。

---

## 2. 真机阶梯（当前真实结果，非目标）

驱动：`kernel/recovery/mt_pvr_bridge.ko`（Stage B S1；
在载构建 build-id `894faf50…`，arena + cover-page plan + kick-inspect）
节点：`/dev/dri/renderD128`（桥，`pvr`）；
`mt_guest_probe` 已绑定 `00:0e.0`（Guest/FW `2/2` pinned）。
`live_3d_drm` 另注册 `card2`/`renderD129`（`mtvgpu`，与桥节点无关）；
显示走 QXL `card0`，与 S3000 无关。

| 步骤 | 符号 | 返回 |
|---|---|---|
| 1 | `PVRSRVConnect` | **0** |
| 2 | `PVRSRVConnectionCreateDevice` | **0** |
| 3 | `RGXCreateDeviceMemContext` | **0** |
| 4 | `RGXCreateRenderContext` | **0** |
| 5 | `CreateSyncPrim` | **0** |
| 6 | `RGXCreateKickSyncContextCCB` | **0** |
| 7 | `RGXDestroyKickSyncContext` | **0** |
| 8 | `RGXCreateComputeContext` | **0** |
| 9 | `RGXDestroyComputeContext` | **0** |
| 10 | `RGXCreateZSBuffer` | **0** |
| 11 | `RGXDestroyZSBuffer` | void（early path，无 bridge 调用） |
| 12 | `RGXKickSync`（经 `0x88:0x4` 提交） | **0**（accept-and-inspect，即时 fence） |

约 128 条记录中无失败的桥命令和同步 ioctl，
进程 `exit=0`，dmesg 无 WARN/BUG/Oops。
`0x81:0x5` 及真正的 TA3D kick 翻译仍未开始——
那是 S4-3 translator 边界（DM 队列格式未知 + 非零 CCB 待观察），
不是本桥的拒绝 bug。GPU 真实执行经 `live_3d` 路径已验证（r66/r70/r71），
与本阶梯相互独立。
补充（r72–r78）：`RGXKickSync` 手工结构体可发出非零 check 包
（`0x88:0x4 check=1`，fabricated 6/6 + 活体 `ufo_known=1/1`）；
`0x88:0x0` 的 CCB pack 公式为 `(arg5&0xff)<<8|(arg4&0xff)`；
update 侧不在 `RGXKickSync` 路径（证伪），候选 `RGXKickSyncDDK2`
需桥广播新 DDK 特性（`features+0x54>=2`，桥故意钉死 legacy，r78），
改桥 + 重载单独立项。
补充（r141–r184）：`drm_major=2` 下 DDK2 同链（render2、CCB2 建销、
sync event）全绿；真实 `musa_blit_test` 发出首个真实 `0x89:0xa`
（CCB `0x8000f44000`/`0x1200`，桥 accept-and-log，39/39 字节落定）；
`translate_transfer` dry-run 程序字节与离线预言一致；
TQX flavor-1 上下文 bring-up 打通（`tqx-ctx: ready`，无提交）。
真提交入口的 `-25`/`-ENOTTY` 仍是 S4 边界（设计拒绝，非缺陷）。

第 4 步的完整失败链（全部由 gdb 实测，不是推测）：

```
RGXCreateRenderContext
 └ FUN_00183050  RGXCreateDevmemBufferMemHeap
    └ MTSRVSubAllocDeviceMemMIW(len=0x9000, align=0x1000, name="MemHeap:PDS_CODE")
       └ DevmemXAllocVirtual → VMRA → RA_Alloc_Range → _SegmentSplit 失败
          ⇒ 323 = MTSRV_ERROR_RA_REQUEST_ALLOC_FAIL
       ⇒ UMD 转换 ⇒ 83 = MTSRV_ERROR_DEVICEMEM_OUT_OF_DEVICE_VM
```

**36 KiB 的请求塞进 32 KiB 的堆**，因为 `"PDS Code and Data"` 被我们绑在
`size = 0x8000` 的 slot 7 上。

---

## 3. 本轮修掉的真实缺陷

按发现顺序，全部为离线测试**抓不到**、只有真机 + 真实 UMD 才暴露的问题。

| # | 缺陷 | 后果 | 提交 |
|---|---|---|---|
| 1 | `pvr_mmap` **use-after-free**：解锁后仍读 `pmr->bytes`/`pmr->host`，而 `0x86:0x5` 会 `vfree+kfree` 同一 PMR | 野指针可经 `remap_pfn_range` 映射进用户态 | bA26 |
| 2 | `0x6:0x1e` HeapCfgHeapCount **字段顺序错**：厂商 OUT 是 `{eError; numHeaps}`，eError 在前 | UMD 把 count 当错误码，缓存「零个堆」 | bA25 |
| 3 | `0x6:0x11` DevmemIntHeapCreate **路由错**：与 `0x13` 共用 PMR-map handler | 合法请求被判 `-EINVAL` | bA25 |
| 4 | `0x6:0x12` DevmemIntHeapDestroy **是空桩** | UMD 二次释放 → `double free` | bA26 |
| 5 | 同上 handler **重复加锁**（`file->lock` 非递归） | **自死锁**，两个进程卡在 `D` 态，`rmmod` 不了，**需重启** | bA26 |
| 6 | `0x6:0x10` DevmemIntCtxDestroy **与 0x12 共用 fallthrough**，只找 `KIND_HEAP` | ctx 是 `KIND_CONTEXT` → 返回 `-ENOENT` | bA27 |
| 7 | `ctx_create` **预分配 11 个堆对象**，UMD 从没见过、也不会归还 | 句柄对不上 | bA27 |
| 8 | **`0x6:0x20` 索引字段用错**：读 `ui32HeapConfigIndex`（UMD 恒传 0），应为 `ui32HeapIndex` | 11 次调用全返回 heap 0 → 缓存 11 份 "General" → 名字查找必失败 → **`double free` 的真正根因** | bA27 |
| 9 | 堆表**发布空槽**（slot 4/5 base=0 size=0），且 count 写死 11 | UMD 为每个条目建 arena，空条目无法保留 VA → 错误 82 | bA28 |
| 10 | `"Component Control"` 被放在**空槽位 4**，从未真正发布 | 名字数组与几何错位一格 | bA28 |
| 11 | `0x6:0x27` MTGPUUpdateOOMStats **未实现**（`-ENOTTY`） | render context 直接失败 | bA29 |
| 12 | `0x6:0x14` / `0x6:0x16` UnmapPMR / UnreserveRange **未实现** | 每轮拆除各被拒一次 → 错误 38 | bA30 |
| 13 | `0x82:0x9` RGXDestroyRenderContext **未实现** | 拆除不完整 | bA30 |
| 14 | 桥接堆表误用 Windows 11 项物理计划 | PDS/USC 名字落在 32 KiB / 4 KiB 槽，装不下 36 KiB 请求 → 错误 83 | bA33 |
| 15 | `pvr_mmap` 用页对齐后的 VMA 长度直接对比 PMR 字节数 | 39935 / 174079 等非整页映射被拒，UMD 解引用失败地址后崩溃 | bA33 |
| 16 | `0x40206441` sync rename **未实现**（`-EINVAL`） | render context 收尾失败，错误 38 | bA33 |
| 17 | `0x6:0x3` 与 `0x6:0x6` 共用 28 字节 OUT handler | 12 字节 OUT 被判 `-EINVAL`，`CreateSyncPrim` 报 37 | bA33 |
| 18 | `0x6:0x4` PmrUnmakeLocalImportHandle **未实现** | 同步事件收尾被拒 | bA33 |
| 19 | `0x88:0x0/0x1` kick-sync context 创建/销毁**未实现** | CCB 返回 37 | bA34 |
| 20 | `0x81:0x0/0x1` compute context 创建/销毁**未实现** | CCB 返回 37 | bA35 |
| 21 | `0x82:0x2/0x3` ZSBuffer 建销 handler **未实现**；配方中 heap/连接参数顺序勘误 | 两次用户态段错误（传反了） | bA37 |
| 22 | reservation/map/unmap/unreserve 全是空桩成功 | 未来页表无 range 可编程、无从知道映射死活 | bA38 |
| 23 | `0x88:0x2/0x3/0x4` 提交入口**未实现** | kick 返回 37（后改为 accept-and-inspect + 只读观察，r63） | bA36 |
| 24 | `dma_source_release()` 用 `page_pa` 做 `dma_unmap_page` | 把从未 map 过的 GPU PA 交给 DMA API 并泄漏映射（仅 init-失败路径，只在 r58 复核中开火前被抓到） | r58，已修 |
| 25 | 跨模块 `symbol_get` + `EXPORT_SYMBOL` 契约设计 | `__symbol_get` 在本内核不解析（连 printk 都 NULL），整条路线作废；桥改 PCI 查找 + drvdata + `try_module_get` + 核心 DMA 直连，header 只剩地址形状 | bA39/bA40 设计已取代 |

第 8 项值得单独强调：**`double free` 只是三层之外的表象**。
整条链路上一个错误码都没有暴露（50 条桥命令全部 ret=0），
是加了一行 `pr_info` 打印 UMD 实际传的参数才看出来的。

---

## 4. bA33：堆名映射已按厂商蓝图修正（原“当前墙”已消除）

### 已证明的事实

**(a) 尺寸是对的。** 直接从 `mtkm64.sys`（sha256 `0512ad5a…`，与报告引用一致）
解出权威 22 格堆表：

- 表地址：反编译给出 `MMU+0x108 == 1` 选 `0x141030fa0`，否则 `0x141030d90`
- **布局**：步长 `0x18`，表首在标称地址 **+8**，base 在 `+0x8`、size 在 `+0x10`
- 该布局由**扫描**得出（只有它能逐字复现现有 plan），测试 `test_decoded_layout_reproduces_our_plan` 断言之

结论：slot 7 = **32768**、slot 8 = **4096**，与我们的 plan 一致。
**「把堆改大」被这条证据否定。**

**(b) 名字是错的。** Windows 只给 13 个**资源**命名，**从不给 22 个堆命名**
（`reports/windows-kmd-crosscheck.md:61-64` 已记录此缺口）。
所以「哪个格子叫 PDS Code and Data」一直是**我们的推断**，现在被证伪。

**(c) 决定性反证：具名资源必须装得下。**
`decompiled/mtkm64.sys/decompiled.c:23004-23083`（`FUN_14001ccd8`）：

| 资源名 | 大小 | 目标堆 |
|---|---|---|
| **"Static PDS"** | **0x100000 (1 MiB)** | 见 `puVar27[4]` |
| "Dynamic PDS" | 0x200000 | — |
| **"Static USC"** | **0x100000 (1 MiB)** | **puVar27[4] = 2**（明确钉住） |
| "Texture state" | 0x200000 | 10 |
| "Paging Command" | 0x400000 | — |
| "PMVA" | 0x20000000 | 0x11 |
| "Free List" | 0x1000000 | 3 |

能装 1 MiB 具名资源的槽位只有：**0, 1, 2, 3, 6, 9, 10**。
**slot 7（32 KiB）和 slot 8（4 KiB）不在其中**，而我们恰恰把两个名字绑在那里。

与 UMD 实测**完全吻合**：它要 `0x9000` (36 KiB) 给 `MemHeap:PDS_CODE`。

### 结论

桥接堆表已改为厂商 `gasRGXHeapLayoutApp` 的 15 项蓝图，
`PDS Code and Data` 在 `0xda00000000+0x100000000`，
`USC Code` 在 `0xe000000000+0x100000000`。该表的
每一项都已用随包预编译对象逐项核对，不再依赖 Windows 物理计划推断名字。

---

## 5. 下一步（按序；r316 后更新）

1. **ref 审计收尾**：defaults 差分 Δ0 已证 maps 无罪（r184）；
   kill-while-busy 关账轮（故意挂起再杀 + 四点采样）待可重载窗口
   （需批准）；释放语义在归因确认前不动。
2. **真实绘制 transfer-fill 闭环（r316 完成）**：UMD 全链条（建链→提交→
   同步→GPU 执行→落池→比对）`Test PASS (exit=0)`；fire 独立模块收敛为
   正式验证工具（r288 决策），桥侧三开（`=2` + tqx_ctx + fire + bump +
   to_dst）在 holder 窗口可重复。余量是扩展（TA/3D、copy 路径、更大几何），
   不是证伪。
3. **同步 update 语义 UMD 驱动验证**：translator 值语义已闭环（r222；
   ping 工具）；UMD 驱动的 update 数组（submit3 `update=2`）经 bump 写 1
   已满足 SyncPrimWait（r299），回写时序（先写回后交 fd，r159）在真实
   提交下成立。验证前不视为已支持。
4. **DDK2 TA/CDM 专属提交**：`0x82:0xC` 与 `0x81:0x5` 仍是 S4 真提交边界；
   空 marker 结果不推及这些路径。真实工作包与输入规约确认后再推进。

长期项：快照 §7 的门禁可复现（`build/` 产物入 git）与 in-tree 构建外移。

## 6. 质量门禁现状

| 门禁 | 结果 |
|---|---|
| Python 测试 | **394 项通过（1 skip）**（r331 头文件拼写 8 项 + r312 poolbox 门 + r308 颜色覆盖 + r306 普查/pristine + r304 定向 + r297 bump 8 项 + r282 独立模块 12 项；余见下表） |
| C RAM 模型测试 | **299 checks**（r179 fill 构造器 16 项；r181–r182 复核全绿；r304 CCB 目的回环自测 +2） |
| 内核构建 | `W=1` 0 error / 0 warning |
| ABI 门（`mt_guest` 共享结构 + 7 结构 pahole 摘要） | PASS |
| 节点探针 `pvr_node_probe` | 0 failing step、0 value mismatch |
| 内核 dmesg | 零 WARN / BUG / Oops |
| 伪造模式回归 | 仍复现历史 4 步全 0 |

**门禁文件**（每一个都做了**反向验证**，注入 bug 确认能被抓到，再还原）：

| 文件 | 项数 | 守住什么 |
|---|---|---|
| `test_pvr_pmr_lifetime.py` | 58 | PMR 引用计数、mmap 逐页 vmalloc 转换判空、arena 页表复用与 close 回收（r151 增补）；`0x6:0x3/0x6:0x4` 各有独立 handler 且探针覆盖 |
| `test_pvr_heap_index_field.py` | 6 | `heap_index` 而非 `heap_config_index`；探针按名查找 |
| `test_pvr_heap_table_geometry.py` | 7 | 压掉空槽；count 由实际存入数派生；厂商蓝图逐项核对 |
| `test_windows_heap_table_decoded.py` | 10 | 从驱动二进制解表并逐字比对；MMU mode 差异；物理表与 PVR 蓝图不再混用 |
| `test_pvr_heap_name_evidence.py` | 5 | 厂商蓝图上的 PDS/USC 槽位与桥接初始化一致 |
| `test_pvr_kick_packet.py` | kick 包 + inspect | `0x88:0x4` 84 字节字段偏移（编译期 offsetof）与两次真实捕获；inspect 路径只用结构体、无裸偏移读、失败只降级 |
| `test_pvr_session_ops.py` | bind-path prereqs | 40 位 mask 显式设置；无符号表机制（`__symbol_get` 不可用，不断言 export）；S3000 槽位字面量仅存宏定义一处（r187） |
| `test_live_tqx_dma_source.py` | DMA 源 | TQX DMA-source 路径的 IOVA/GPU-PA 分离 |
| `test_pvr_tdm_shmem.py` | 5 | `0x89` TDM 共享内存桥（r88；离线实现，未加载） |
| `test_pvr_tdm_context2.py` | 4 | TransferContext2 建销与 token（r150） |
| `test_pvr_tdm_submit3.py` | 20 | SubmitTransfer3 observe/dry-run/digest/清单/CCB 定向/VA 普查/pristine/形态/边界门禁（r174/r181/r182；r267 locate；r302/r304/r306/r310/r312） |
| `test_pvr_tqx_fire.py` | 18 | 分块/串行/落池/颜色/池基门禁 + 调度不等 fence 等/ work 验拷/teardown 中止优先（r267/r280/r290/r300/r308/r315；含反向） |
| `test_pvr_submit3_bump.py` | 8 | update 回写：opt-in 开关/32 上限/用户数组拷贝/先解后写/UMD 自值/空柄跳过/observe 接线（含反向；r297/r299） |
| `test_pvr_live_tqx_fire.py` | 12 | 独立模块：自有节点/无桥依赖/64 页/slices 锁序/分块上限/单发/等待锁外/teardown 对称/忙门上报/无前置完成门（含反向；r282/r283） |
| `test_pvr_tqx_slices.py` | 9 | TQX slices bring-up 调度/copy prepare/非致命标志/teardown 释放/读镜像/零执行/scratch 预绑/trial_lock 分段/忙门上报（含反向；r261/r263/r265/r283） |
| `test_pvr_kickta3d5_observe.py` | 7 | KickTA3D5 observe 路由/定界/鉴权/零嵌套读/标量上报/零执行/零填充回 0（含反向；r215，未加载） |
| `test_pvr_observe_ping.py` | 11 | ping 工具源码门禁：`0x82:0x14`/ENOENT 期望 + `0x82:0x1f`/ENOTTY control + wire 结构体 + 非零句柄 + 全路径 envelope/fire/teardown + 非零相预置/重 fire + CCB 相载入/fire + 负向定界双 errno（含反向；r217/r218/r224/r225/r238） |
| `test_pvr_syncprimset.py` | 7 | SyncPrimSet 真写路由/ABI/解析复用/定界/仅 host 写/零执行/零填充回 0（含反向；r220，未加载） |
| `test_pvr_translator.py` | 7 | translator 默认关闭/check-only 路由/update fence 后写回/期望值记录/sync-block 跟随/check 等待门控 ncheck（含反向；r126/r147–r148/r159/r222） |
| `test_pvr_update_writeback.py` | 6 | 写回探针工具源码门禁：update 接线/回 0 期望/check 接线/4s 计时断言/teardown/双 PMR/双 update/fence poll（含反向；r223/r227/r231/r233/r236） |
| `test_pvr_fn_ids.py` | 2 | 57 分发功能号逐值钉死 + dispatch 零裸标签（r188；r215 加 `RGXKICKTA3D5=0x14`；r222 加 `SYNCPRIMCPUSIGNAL=0xa`） |
| `test_pvr_multicore_info.py` | 3 | `0x1:0xc` 回显 caps、单核（r152） |
| `test_pvr_ddk2_render2.py` | 5 | DDK2 render 建销（r142；OUT 以活体 12 为准；r220/r222 改判 SyncPrimSet 路由） |
| `test_pvr_ddk2_kicksync2.py` | 3 | DDK2 CCB 建销（r143） |
| `test_pvr_drm_major_gate.py` | 3 | 桥 `drm_major` 参数选路（r134；只读，不加载） |
| `test_pvr_heap_layout.py` | 2 | 堆几何基础断言 |
| `test_pvr_shim_drm_major.py` | 3 | shim major 默认 1/opt-in 2/非法值回退（r157） |
| `test_pvr_shim_shared_backing.py` | 1 | shared backing 别名/隔离 + 提交前 snapshot（r155） |
| `test_pvr_shim_ccb_resolve.py` | 1 | CCB VA→PMR 归属 + runs 形状 + 越界/反向（r158/r160） |
| `test_pvr_addr_plan.py` | 5 | scene VA 预设逐值钉死 + 8 文件 include + code 区无裸字面量 + 别名引宏（r186；r188 补 stream/slot；r267 space 2112 + scratch） |
| `test_pvr_kicksync_fn.py` | 2 | `0x88` 功能号逐值钉死 + submit 按名比较（r187；r188 改全名） |
| `test_pvr_object_find.py` | 3 | handle+kind 统一查找 + 8 函数调 helper + map 单次查找（r189） |
| C: `pvr_arena_plan_test` / `system_dma_pages_test` | plan/DMA 页 | arena + per-page 绑定覆盖 12 kick ranges；DMA 页解析与线性连续守卫 |

设计要点：`test_pvr_heap_name_evidence.py` 已从“已知缺陷记录”
改为“正确映射断言”。厂商蓝图一旦漂移，它会直接失败。

---

## 7. 待办（用户已提出，尚未动手）

### 目录结构：**建议局部调整，不建议大重构**

实测数据（2026-10-03 重测；bA32 原值已过期）：

| 目录 | 规模 | 问题 |
|---|---|---|
| `build/` | **5.9 G** | 全 gitignore，但**主门禁依赖它** |
| `decompiled/` | 2.5 G | gitignore，合理 |
| `tools/` `downloads/` | 882 M / 659 M | gitignore，合理 |
| `reports/` | 442 文件 | 证据链，不宜改名（索引见 `reports/README.md`） |
| `tests/` | 52 `.c` + 31 `.py` | C 与 Python 混放 |
| `kernel/recovery/` | 171 文件，其中 54 个 C/H 源码 | **构建产物落源码目录**（in-tree 构建） |

三个**真实**问题（2026-10-03 复核状态）：

1. **门禁不可复现（仍未决，最严重）**。`scripts/verify-runtime-integration.py` 仍要求
   `build/recovery-channel/loaded-6f259*.ko` 恰好存在一个，而 `build/` 是
   gitignore、git 跟踪数为 0。**在新机器 clone 上这个主门禁直接 `raise` 失败。**
   替代方案：把该 `.ko` 或其 pahole 结构摘要（几 KB，可 diff）纳入 git。

2. **内核构建 in-tree（仍未决）**。`M=$(CURDIR)` 让产物落进源码目录。改 `M=$(BUILD)` 即可外移。

3. ~~无顶层 Makefile~~ —— **已解决**（bA36 落地为 `mt-vgpu-guest/Makefile`，见下）。

**不建议**做的：拆 `tests/` 子目录（交叉引用全改，收益低风险高）、
动 `reports/`（毁证据链）、重排 `kernel/recovery`（24 个模块有加载顺序依赖）。

### 按步骤测试：建议分四层

| 层 | 内容 | 需 root/硬件 | 时长 |
|---|---|---|---|
| **L1 纯离线** | 221 py + 268 C checks | 否 | ~3 s |
| **L2 构建+ABI** | `W=1`、ABI 漂移、线尺寸门 | 否 | ~90 s |
| **L3 节点探针** | 加载模块、`pvr_node_probe` | 是（不碰硬件） | ~2 s |
| **L4 UMD 端到端** | 真实 UMD 阶梯（8 级已落地，见下） | 是 | 每级几秒 |

L4 阶梯式已落地（`make umd` 8 rung，逐级打印、每级独立 trace）。
**活会话上禁用 L3/L4**：它们会 rmmod/insmod（见 `STATUS.md` 红线）。

### Make 流程规范化：目标接口（bA36 已落地）

```
make check          # L1+L2，默认门禁，不碰硬件
make check-offline  # 仅 L1
make probe          # L3（自动 insmod/rmmod，必须用 trap 保证清理）
make umd            # L4 阶梯 8 级，逐级打印，每级独立 trace
make kernel         # 全部模块 W=1 构建，不加载
make clean          # 含 in-tree 产物
make help
```

关键约束：**`make check` 绝不能加载模块或碰 PCI**；
L3/L4 必须用 `trap` 保证 `rmmod`——这正是 bA26 那个 `D` 态自死锁的教训。
**但活会话上 L3/L4 一律禁用**（它们先 rmmod，见 `STATUS.md` 红线）；
此前的“结束后模块已卸载”验证是在可重建会话上做的。

教训：make 变量展开发生在 shell 引号移除之后，
配方里的 `'b5*+0'` 会带着引号原文到达 harness（必须不带引号）。
所有 rung 经 `eval` 转一手，让引号被重新处理——
render 级曾因此静默地跑错参数，修完后 8 级全绿。

---

## 8. 方法论教训（本轮新增，均已写入 `MEMORY.md`）

### 8.1 「什么都没匹配上」不能当阴性结论

本轮我犯了**两次**同类错误，两个 0 长得一样，含义完全不同：

1. `strings mt_pvr_bridge.ko | grep "self-deadlock"` → 返回 0，
   我当成「修复已编入」。**注释永远不会出现在二进制里**，这条检查什么都不能证明。
2. `objdump | awk '/<pvr_cmd_heap_destroy>:/,...'` → 返回 0 个 mutex 调用，
   我当成「没有锁」。实际是**该函数被编译器内联了，根本不是符号**，匹配不到而已。

**有效做法**：内核构建的 `mutex_lock` 是 PLT 调用，须读**重定位表**并归属到函数：

```sh
objdump -dr ... | awk '/^[0-9a-f]+ <.*>:/ {fn=$2}
                         /R_X86_64_PLT32\tmutex_lock/ {print fn}'
```

得到 4 处 lock 全在顶层入口函数、零嵌套——这才是有效证据。

（另：`modinfo` 命令**本机根本没装**，曾让我一度以为 vermagic 不匹配。
正确做法是 `objcopy -O binary --only-section=.modinfo` 读段。）

### 8.2 gdb 断点必须实测，不能从反编译推

我多次把断点下在**从反编译读出的地址**上
（`FUN_00183020`、`FUN_00132ce0`、`FUN_00133960`、`FUN_00132ce0`），
**全部没命中**——反编译的调用关系与真实执行路径对不上。

**有效做法**：
1. 断 `PVRSRVGetErrorString`，一次同时拿到**错误码**和 `bt`（→ 拿到 83）；
2. 从 `PVRSRVDebugPrintf` 的调用点**反查**是哪个桥命令；
3. 要取函数返回值，在其**返回地址** `*(void**)$rsp` 下断点，
   不要在 breakpoint 的 `commands` 里用 `finish`（不给返回值）。

**读代码不如读日志。**

### 8.3 既有教训（沿用，仍然有效）

1. **在伪造 shim 下验证过的配方，必须对真驱动重新推导。** bA13 的 `u0` 藏了四轮。
2. **当结论建立在「内核返回 EFAULT」上时，先怀疑探针。**
   6.12 的 `struct drm_unique` 是 `{unique_len, unique}`（我一开始弄反了）；
   本轮又两次把注释当代码、把内联函数当符号。
3. **`/tmp` 满会伪装成假回归**（测试数 120→91 + `cc` 错误）。
4. **易失路径要优先从树内留档取。** 本轮重启后 `/tmp/mtt-linux-umd-5.2.0/` 被清空，
   UMD 丢失；已从 `build/legacy-umd-pvr-connect-candidate/rootfs/...` 恢复，
   build-id `429e03c4…` 与原文件逐字一致。

### 8.4 运维提醒

`pkill -f` 的模式串若出现在我自己 shell 的命令行里，会**把 shell 一起杀掉**（本轮踩两次）。
清理残留请用 `pkill -9 -x <name>`（`-x` 精确匹配进程名，不匹配命令行）。

### 8.5 device-mutex 没有外部解锁（r67；与 bA38 同类，不同 mutex）

UMD 内部 MapPMR 卡死在 `device_lock`，持锁者已死（owner 与任何活 task 无关），
`dev->mutex` 全局泄漏：所有 MapPMR 永久挂起（不是降级，是挂起），bridge rmmod
解不掉（锁属 PCI core），**唯一干净恢复是重启**。
触发者无法指认（03:12 前最后一次成功加锁是 02:38 的 cover-probe，其间无 bridge
ioctl 在飞）——结论：**`timeout` + bridge ioctl 的组合必须先论证超时后无持锁
可能**。执行版纪律：DMA 路径命令一律 `timeout 120` 包装只做挂起探测，
超时即停手、不堆任务（D 态任务杀不掉，堆一个多一份永久泄漏风险）。
`timeout` 本身不背锅（TERM 只杀跑得动的），背锅的是临界区内被杀。

---

## 9. S4-3 范围（RGX 真实执行经我方桥）

S4-2 证明了固件通道执行（TQX/3D fills）。S4-3 = 让 MUSA UMD 的 kick
经我方桥真实执行。已探明：

- **不需要找 RGX 固件 blob**：厂商 `mtgpu.ko` 的 firmware 请求表只有
  VPU（`mtvpu-*.bin`）+ META（`musa.fw.1.0.0.0[.vz.linux|.vz.win]`），
  无 RGX 图形固件。vGPU 下 host 拥有物理 GPU 与固件，
  guest 只经 BAR/共享内存环提交——正是 `mt_guest_probe` 已打通的通道。
- **三块的落地状态**（都在我方桥一侧）：
  1. ✅ PMR 进 GPU 可见内存：桥 PMR 改 file-arena backing（r60），
     DMA 经 PCI 直连注册（r45–r49，`dma_addr` 只供 unmap，
     `gpu_pa` 由 Guest 窗口翻译）；`live_tqx`/`live_tqx_readback`、
     `pvr_dma_smoke` 覆盖回读验证。
  2. ✅ VA→PA 页表输入：reservation 台账（bA38/bA41）+ CPU-only plan
     （r50/r51）+ cover-page 绑定与独占策略（r61，`fallbacks=0`）。
     plan 仍不上载不执行；translator 用到 cover 近似时需重审（r61 边界）。
  3. ⏳ kick 翻译：`0x88:0x4` 仍 accept-and-inspect + 即时 fence；
     T1/T2 只读观察已落桥（r63），T3 缺 DM 队列格式 + 非零 CCB 内容。
     在 handoff 就绪前，`-ENOTTY`（`0x81:0x5` 等真提交入口）仍是 S4 边界。
- **不碰**：PCI 绑定（probe 已持有）、固件加载（已是 GE2/FW2 会话）、
  显示（QXL，与 S3000 无关）。

## 10. S4-3 handoff 设计（bridge → probe 会话；as-built，bA39/bA40 方案已作废）

目标：UMD 经桥分配的 PMR / 预留的 VA / 提交的 kick，
最终变成 firmware 会话里的真实 DMA + 页表 + 执行。

bA39/bA40 的跨模块符号表契约（`symbol_get` + 版本号）已被实测推翻：
`__symbol_get()` 在本内核上不解析（连 printk 都返回 NULL）。
as-built 机制（`da3df8b`，r45–r63）：

1. **PMR 内容进 GPU 可见内存**：桥在 MapPMR 时经 PCI 查找到 live 会话设备
   （驱动名校验 + drvdata + `try_module_get`，每次调用全量重验），
   用核心 `dma_map_page()` 建 DMA 映射；`gpu_pa` 由 Guest system-memory
   窗口翻译，**绝不从 `dma_addr` 假设**。任一步失败即回退纯系统内存语义，
   永不对 UMD 报错。`mt_pvr_session.h` 只剩地址形状，无函数表、无版本号、
   无 export。`try_module_get` 只防卸载不防 unbind——操作纪律：
   先 rmmod 桥（PMR 释放时 unmap 全清），再碰会话模块（见 §8.5 同类教训）。
2. **VA→PA 页表输入**：reservation 台账（bA38/bA41）+ `mt_gpu_vm` plan
   （r50/r51）+ cover-page 绑定与先占独占（r61）。
   风险点（仍成立）：VA 分配权在 UMD（byte-tight，非页对齐），
   未对齐 prefix/tail 是整页近似（邻居字节同页可见，正是独占策略要拦的）；
   translator 用到时重审该近似。
3. **kick 翻译**：`0x88:0x4` 经 accept-and-inspect 后，T1（UMD 内存拷贝数组，
   bridge dispatch 在调用进程上下文，`copy_from_user` 可达）+ T2
   （UFO 句柄 → bridge PMR/对象 → GPU PA + offset，验值）已落成只读观察；
   T3（按 firmware 环格式重编进 DM 队列）待 RGX 环格式反推。
   在 handoff 就绪前，真提交入口的 `-ENOTTY` 仍是 S4 边界。

顺序：1→2→3，每步独立可验证（1 只需 DMA 回读比对，不执行；
2 只需页表 image 逐字节核对，不上传；3 先审包不上交）。
1、2 已真机验证（r49/r51/r60/r61）；3 的 T1+T2 已落桥实测（r63）。

## 11. 下个硬件窗口的验证清单（按序；transfer-fill 已闭环，余 TA/3D 与 copy）

上一版清单已完成：① DMA mask 显式化（bA43 代码 + r46 重启首绑核验 40）；
② DMA 回读比对（r49 GPU-PA 窗口转换 + TQX 回读成功）；
③ 新 probe 构建上机（r47/r51 新构建已加载建会话，非“未加载”）。

当前清单（r316 后更新；transfer-fill 真实绘制已闭环）：

1. **真实绘制 transfer-fill 闭环（r316 完成）**：UMD 全链条 `Test PASS
   (exit=0)`——建链（DDK2 render2/CCB2）→ 真实 `0x89:0xa`（VA/尺寸稳定，
   `+0x40` 轮变 + 偶发 +1 字节）→ translator bring-up → UMD 矩形分块
   fire（GPU 执行 + fence + 逐块验）→ 落 UMD 目的池（`dest+0 vs
   source+3841`，GDB 点名）→ sync bump 满足 → 像素比对通过。fire 独立
   模块收敛为正式验证工具；桥三开在 holder 窗口可重复（桌面 stop，
   用户协调制）。
2. **TA/3D 专属提交**：`0x82:0xC` 与 `0x81:0x5` 仍是 S4 真提交边界；
   真实工作包与输入规约确认后再推进（blit 在 submit3 后 hanging 是
   已知前置：SyncPrimWait 等同步值约 100 秒后自杀，满足即继续）。
3. **copy 路径与更大几何**：blit `-f` 走 fill；copy（源→目）与 4K 级
   矩形未覆盖。fire 分块上限 64（r287 顶满验证），超限 `-E2BIG`
   大声拒绝（r289）。
4. 对象存储已满：需空存储的实验（含再次的 `live_3d`）会被 `-EBUSY` 拒绝；
   下一次需空存储的实验必须等新会话（重启 + 重建），不能插队。

## 12. 运行态（2026-10-08 更新；本节是活页）



- r422 (2026-10-09): TA buffer Header+Entries dual-zone root-cause for r421 timeout (offline disasm): RGXSubmitTA (FUN_001796b0, decompiled.c:54365) reads back 9 qwords from TA_buf+0x10/0x18/0x20/0x28/0x30/0x38/0x40/0x48/0x60 into psKickTA [MEASURED]; RGXPrepareTA (FUN_00178800) writes Header covering 0x00-0x160 [MEASURED]; our mt_ta_real_buffer_build() writes 40B Entry at buf+0x00 (mt_ta_real.h:178-179), Q2/Q3/Q4 (0x10-0x27) clobber Header fields at 0x10/0x18/0x20 -> firmware reads garbage Header via psKickTA -> hang timeout; r414 all-zero = empty Header = no-work -> 219us success; r421 non-zero Entry = polluted Header -> timeout; Q1=target_va (in-Entry) not falsified but wrong Entry location is the more direct cause; Entries real container unknown (544B buffer inferred); r423 prereqs: P0 determine Entries container or Header-only test (TA_buf+0x10=target_va only), P1 fix mt_ta_real_buffer_build() to not write buf+0, T5 Header-integrity gate pending; zero HW touch, no code changes, local commit not pushed.
- r421 (2026-10-09): Q0-corrected live TA readback (highest-risk): dual-gate test build (MT_TA_REAL_PACKET=1+MT_TA_READBACK_DEBUG=1, static_asserts temporarily neutralized, reverted after; make kernel W=1 zero warnings); pre-live T1/T2/T3/T4 all pass (13 tests); post-cold-reboot probe full-param load, trial rebuilt (connect=0 pinned=1); dual-gate bridge loaded, /dev/dri/renderD128 ready; mt-ta-readback full chain: ctx 0x1000, 11 BOs + 12th target BO (va=0x7b000000 bytes=16384) bound; 0xFD submit (Q0=0x48000000000 flags-only [MEASURED], Q1=0x7b000000 [INFERRED]) -> fence allocated -> firmware 5s timeout (-ETIMEDOUT, submitted-but-ignored); vs r414 (Q0=0/Q1=0, 0x100 in 219us): Q0 pollution not the sole cause, Q1=target_va stays [INFERRED]; pending TA fence -> bridge ref=1, safe_rmmod.sh correctly refused (no -f), awaiting user cold reboot; dmesg clean; gate 543 Python + 630 C green, kernel W=1 zero warnings, tree reverted clean, local commit not pushed.

- r411 (2026-10-09): Real TA packet infra (offline): new kernel/mt_ta_real.h (MT_TA_REAL_PACKET gate default 0, MT_TA_CMD_BUFFER_BYTES=0x168, 40B entry struct, mt_ta_entry_simple_build with Q2 dim packing [MEASURED]); mt_marker_fence.h integrated mt_fw_ta_real_command (#if-gated, VA @+0x28/size @+0x30 [INFERRED] by 3D analogy, TO-VALIDATE); mt_ta_submit_build dispatches on gate; new tests/ta/test_ta_real.py (6 tests); 360B DMA/VA mapping deferred (separate prereq); gate 480+299 green, kernel W=1 zero warnings, reverse validation passed, local commit not pushed.
- r404 (2026-10-09): Dispatch split (offline): pvr_bridge_dispatch (168 lines) split into 8 per-group helpers, main keeps ENOTCONN + outer routing; pvr_translator_prepare_locked (294 lines) analyzed and kept as-is (clear 6-phase linear structure, centralized teardown); 13 text-scan tests updated for helper locations; reverse validation passed; gate 474+299 green, kernel W=1 zero warnings, local commit not pushed.
- r397（2026-10-09）：render_ctx 双执行上下文落地（活体）：exec_ctx_3d（node_type=5→DM2）+ exec_ctx_ta（node_type=2→DM3）共享 process；kick 有-context 传真实 ctx（dm=3 门禁通过，借用不 kfree），无-context throwaway 回退；活体 V1/marker 回归/V3 隔离全绿，dmesg 干净；门禁 474+299 全绿，kernel W=1 零警告，本地提交未 push。
- r398（2026-10-09）：R5 Phase 2 完成（离线）：删除 per-file VM（ta_vm_ctx 字段/create/destroy/kick fallback/mt_ta_vm.h/两死亡测试）；无有效 render_ctx 时 kick 直接 -EINVAL；exec_ctx throwaway 删除（ctx 恒借用）；4 测试文件更新；门禁 474+299 全绿，kernel W=1 零警告，反向验证通过，本地提交未 push。
- **r369 wire 6 清除 + kfree 回退上机（真机恢复）**：`mt_drain_pending.ko` 清 wire 6（`drained=1`，DM3 悬挂 marker 安全释放：job.state=EMPTY、m->context=NULL、无 waiter → -ETIMEDOUT signal+put → `mt_marker_release` → refcnt 1→0），drain 模块即卸无残留；旧桥（build `4a78b331`）回滚件已存后干净 `rmmod`；源码回退 r367 的 kfree 删除（r368 证伪 UAF——TA op 从未写 `m->context`，释放为干净释放，消 ~64B/次泄漏），`make kernel` W=1 零警告后按 r360 流程上机新桥（sha256 `4f5b08af…`，一次成功，kallsyms 见 `mt_bridge_submit_ta_work` 导出，`/dev/dri` 正常）；dmesg 零 WARN/BUG/Oops，probe ref=1 未动、bridge ref=0，freeze 恢复。门禁：`check-offline` 411+299 全绿；新 `tests/test_ta_kick_ctx_release.py`（3 tests，反向验证通过）。见 `reports/r369-wire6-drained-kfree-restored.md` + 双证据（0600）。生产 TA 完成路径（r368 缺口）另立轮次。

- **r368 wire 6 只读诊断（零硬件触碰）**：wire 6 悬挂 ~940s 无完成事件，根因为结构性——生产事件路径（probe `mt_runtime_event`→通用 `mt_marker_complete`）拒收 TA 完成码 `0x100`，`mt_marker_complete_ta` 生产零调用（仅 r366 测试模块），且无超时机制 → 永久悬挂；r367 所述 UAF 经代码证伪（TA op 自 r366 从未写 `m->context`，`if (m->context)` 恒假，在载桥 `kfree(ctx)` 为干净释放），r367“修复”实引入 ~64B/次泄漏；恢复方案（待确认）：`mt_drain_pending.ko` 清 wire 6→refcnt 归 0→`rmmod`→回退 kfree 删除→重载；r369+ 须补齐生产 TA 完成路径。桥 refcnt=1，dmesg 零 oops。见 `reports/r368-wire6-uaf-reassessment.md`。
- r371（2026-10-08）：端到端 TA 验证被固件 trial 状态阻塞：冷启动后 0x890=2；probe 改源码接受 0x890==2 后绑定成功，但 mt_trial_start 要求 0x890==0，trial 无法启动（connect=-61）；dispatch 到达但 pvr_session_acquire 返 -ENODEV；r370 完成路径未活体执行；门禁 417+299 全绿，本地提交未 push。
- r372（2026-10-08）：Trial 接受 0x890==2，端到端 TA 验证打通：mt_trial_start 放宽入口检查（0/2），trial 成功建立（connect=0 pinned=1）；真实 0x82:0xC TA kick 两次成功（wire=1/2），0x100 完成到达无超时，无悬挂；门禁 421+299 全绿（含新 test_trial_890.py），make kernel W=1 零警告，反向验证通过；本地提交未 push。
- r373（2026-10-08）：OUT.update_fence 回填验证通过：内核路径一直正常（KMD 头布局一致），r372 的 0 系其 harness 传参 bug；正确 harness 两次活体精确匹配（update_fence=3/4 vs dmesg wire=3/4）；修复：pvr_out 失败改记 pr_warn（不再误导性记 submitted）；门禁 425+299 全绿，make kernel W=1 零警告，反向验证通过；bridge 未重载，本地提交未 push。
- r374（2026-10-08）：R5 per-file GPU VM/BO 后端设计定稿（离线）：TA 命令缓冲经 mt_bo_system_borrow() 进 per-file 设备 store VM（非 store=file facade）；8 步映射流程（pin->borrow->bind->seal/upload）；MT_TA_VM_READY 校验门照抄 TQX；接口头 kernel/mt_ta_vm.h + 6 布局测试反向验证通过；V1-V4 待活体；门禁 425+299 全绿，本地提交未 push.
- r370（2026-10-08）：生产 TA 完成路径实现（桥侧）：pvr_ta_wait_complete 轮询 DM3 等 0x100→mt_marker_complete_ta，超时 pvr_ta_abandon 以 -ETIMEDOUT error-signal；不碰 frozen probe；一次计划内重载成功；活体 TA kick 被 -EHOSTDOWN 阻塞（trial 状态问题）；门禁 417+299 全绿，本地提交未 push。

- **r367 0x82:0xC 真实分发接 submit_ta_work（真机）**：observer（-ENOTTY）改为真实分发 `pvr_cmd_musakickgfx2()`（kernel/recovery/mt_pvr_bridge.c），IN→`mt_ta_submit_params` 映射（D5，kernel/mt_ta_submit.h）。活体（direct ioctl，INIT 建连）：TA+PR kick 诚实拒收 -EOPNOTSUPP✅、TA-only 真实执行（wire=6，OUT.update_fence）✅；fence 语义（T2/T3）受阻——dispatch 成功路径 `kfree(ctx)` 致 UAF（op 已转交 m->context，源码已修，未上机），wire 6 未完成致桥 refcnt=1 无法二次重载。桥计划重载一次（新 build-id 4a78b331），probe 未动；dmesg 无新增 WARN。门禁：check-offline 408+299 全绿、make kernel W=1 零警告、新增 tests/test_ta_kick_dispatch.py（反向验证通过）。见 reports/r367-ta-kick-dispatch-live.md。

- **r366 submit_ta_work 落地并活体验证（真机）**：R2b 第一阶段完成。mt_marker_ops 第 5 个独立 op（kernel/mt_marker_fence.h），DM3/opcode 0x66 marker 构造，MT_FW_TA_COMPLETE_CODE=0x100 入库（kernel/mt_ta_submit.h），桥侧导出 mt_bridge_submit_ta_work（EXPORT_SYMBOL_GPL）。验证模块经真实 op 发送：T1 基础（wire=3，完成码 0x100，fence signaled）✅、T2 check_fence=已完成 wire 即满足✅、T3 非法 id 返 -EINVAL✅、T4 真异步等待（dm1 marker）后 0x100 完成✅，result=0。桥按计划重载一次（r360 流程），probe 全程未动；验证后模块即卸，refs 1/0不变，dmesg 无新增 WARN。门禁：check-offline 402+299 全绿、make kernel W=1 零警告、新增 tests/test_ta_submit_op.py（反向验证通过）。见 reports/r366-submit-ta-work-live-verified.md + dmesg 证服（0600）。

- **r365 DM3 接受 opcode 0x66 marker（真机活体）**：单发空包（DM=3，opcode 0x66）被 firmware 即时消费，回 wire_id 匹配事件（words[1]=0x100，非 FAULT）；对照 opcode 0x64 得标准 COMPLETE（words[1]=0），证明 firmware 区分 opcode；V1/V2 通过，无需 DM4 回退。探针未入库（一次性，build/traces/r365/）。两次 insmod/rmmod 干净，refs 1/0 不变，dmesg 无新增 WARN。见 `reports/r365-ta-marker-dm3-opcode66-accepted.md` + 三证据（0600）。

- **r364 TA firmware 提交通道设计（R4，离线，零硬件触碰）**：`mt_marker_ops` 新增独立 op `submit_ta_work`（与 `submit_tqx_work` 并列）；TA 分配 DM3（推断）、firmware 命令 opcode 候选 `0x66`（推断）；`0x82:0xC` IN 解码为 `struct mt_ta_submit_params`（104B，`kernel/mt_ta_submit.h`，静态断言钉死）；V1–V6 待活体验证清单已列（DM/opcode/`kick_pr`/TA 命令语义/per-file VM/完成事件）。门禁新增 `tests/test_ta_submit_layout.py`（反向验证通过），`check-offline` 402+299 全绿，`make kernel` W=1 零警告。会话未碰，freeze 继续。见 `reports/r364-ta-submit-channel-design.md` + 盘点表证据（0600）。

- **r363 0x82:0xC 活体 IN 观察成功（真机活体）**：r362 修正（`$rsi` 捕获）后 TA 路径一次打通，`SyncPrimRef` → 0，`0x82:0xC`（268B IN）到达桥侧 observer 并解码 （`kick_ta=1/kick_pr=1/kick_3d=0`，`ta_cmd_size=360`，`client_ta_upd_count=1`），返 `-ENOTTY` 未执行；未提交 GPU 工作。新发现：GDB 直 `open()` 的 fd 须补 `ioctl(0x40046445)`（INIT）否则 dispatch 卡 `-ENOTCONN`。dmesg 无新增 WARN，refs 不变，freeze 完好。见 `reports/r363-82c-live-in-observed.md` + 三证据（0600）。

- **r362 描述子根因（离线 fabricated，零硬件触碰）**：r361 的 `+0x18=NULL` 系 GDB 脚本 bug——入口取 `$rdi`（param_1）、返回读 `*(param_1)`，但 `CreateSyncPrim` 把描述子写到 `*param_2`（b10）；实测 `*(param_2)` 处 `+0x18=<ptr>、+0x20=0`（= r352/r353），`*(param_1)` 处 `+0x18=NULL`（= r361 dump）；直接调用 `SyncPrimRef(*b10)` → 0。无需参数调整；r363 唯一前置是修正脚本从 `$rsi` 取值。会话未碰，freeze 继续。见 `reports/r362-desc-mismatch-root-cause.md` + 双证据。

- **r361 0x82:0xC 活体 IN 观察被阻塞（真机）**：复现 r353 GDB 驱动 TA 路径（fabricated 建连 + b10 poke + ASLR 开），b10 描述子 `+0x18` 实测为 NULL（r352/r353 记载为有效指针），`SyncPrimRef` 在 `0xa0fa0: sub 0x30(%rdx),%eax` 处解引用 SIGSEGV，TA 路径无法推进到 `0x92930`；尝试过 SyncPrimRef 入口短路（伪造返回 0）但下游仍崩溃。静态分析确认 `0x36ec0` 构造 268B IN 缓冲（`in_len=0x10c`，从输入结构多偏移经 XMM 打包）。**未发任何 ioctl、未提交 GPU 工作**；freeze 完好（bridge ref 0、probe ref 1），dmesg 无新增 WARN/BUG/Oops。见 `reports/r361-82c-blocked-desc-mismatch.md` + 描述子 dump 证据（0600）。r362 建议：查 `+0x18=NULL` 根因（`CreateSyncPrim` 内部或 `*b7*+176` 差异），或基于静态映射推进 R2 设计。
- **r360 桥重载至 r356（真机活体，用户已批准）**：预检（fuser 无持有者、bridge ref 0、vermagic 一致、observer 串在）→ `rmmod` → `insmod` r356 构建；新桥 build-id `0d6b…55da` == 在盘构建（含 `pvr_cmd_musakickgfx2_observe`），旧 `2a2a…261f` 已下线。dmesg：`unloaded cleanly` → pvr node registered，无新增 WARN/BUG/Oops。活体 connect 健康检查 PASS（`PVRSRVConnectionCreateDevice`→0、`GetSrvHandle` 指针形态、单次 `PVRSRVBridgeCall(1,0)`→0 且 OUT 逐字节命中 `bvnc=0x0023000406600017`）。方法教训：maps 取 bias 须减 file offset；`PVRSRVBridgeCall` 真为 7 参数。probe 未碰（ref 1），bridge ref 0，card0/card1/renderD128 齐全，**freeze 已恢复**。`check-offline` 400+299 全绿；无代码改动。见 `reports/r360-bridge-reload-r356.md` + 四证据（0600）。


- **r359 0x82:0xC 活体观察停轮（真机，安全协议 §2）**：在载桥 build-id `2a2a…261f` ≠ 在盘 r356 构建 `0d6b…55da`（含 observer 串 `musakickgfx2 observe`）；在载桥最后加载于 ~11:26（dmesg `[5635.62]`），早于 r356 提交（14:37）约 3 小时，r356/r357/r358 均未重载桥——在载桥不含 `pvr_cmd_musakickgfx2_observe`，pre-r356 dispatch 对 `0x82:0xC` 只走 `default: return -ENOTTY`（无解码日志）。按 §2 停轮：未发包、未重载桥、freeze 未碰；dmesg 无新增 WARN/BUG/Oops，refs（bridge 0/probe 1）不变。`check-offline` 400+299 全绿；无代码改动。待用户协调重载窗口（r216 流程）后重跑活体观察。见 `reports/r359-bridge-version-blocked.md` + 证据（0600）。

- **r358 UMD 真实建连打通（真机活体，用户已授权）**：`PVRSRVConnectionCreateDevice(&conn,0xffffffff,0xffffffff)`→0（`conn=0x25220fe0`，1ms），`_GetFd` 以 driver 名 `pvr` 首轮命中 `renderD128`，`ioctl(0x40046445)` 后内部 `BridgeConnect`→`GetFeatures`（纯读）→`BridgeAlignmentCheck(1,0xa)`（桥 `pvr_stub_ok` 回零）全绿；`GetSrvHandle`→`0x252211a0` 指针形态；单次显式 `PVRSRVBridgeCall(1,0,in16,out17)`→0，OUT 逐字节命中 `mt_pvr_connect_result`（`bvnc=0x0023000406600017`/`error=0`），桥侧 `pvr_cmd_connect` 收包实证；IN 布局经 `BridgeConnect` 反汇编确认为 `[param_3,param_5,param_4,param_2]`。dmesg 1117→1118 仅+1 行 `arena close`（正常清理），无 WARN/BUG/Oops；`mt_pvr_bridge` ref 0 / `mt_guest_probe` ref 1 不变。未跑 `make probe`（其 `WITH_BRIDGE` 会 rmmod，违反 freeze 红线）；纯 userspace open+ioctl，会话未碰。`check-offline` 400+299 全绿；无代码改动。见 `reports/r358-umd-live-connect.md` + 三证据（0600）。

- **r357 UMD 建连链路 recon（离线 fabricated，零硬件触碰）**：`GetSrvHandle @ 0x3c1c0`=`rdi?*rdi:0` 读连接首 qword（Ghidra 口径 `0x13c1c0`，同一函数，写法统一）；连接 0xd0，首 qword 由 `OpenServicesDevice`（`FUN_00192550`）写入 0x10 services-handle 指针，其首 dword 为 DRM fd；`PVRSRVBridgeCall`（`FUN_00192930`）`ioctl(*param_1, 0xc0206440)`，`ENOTTY`→`0x26`。ctypes 直调真实 `.so` 7/7 走通：dlsym 地址交叉核对、句柄指针返回、NULL→0、`0x6000` 读语义复现、`BridgeCall(0x82,0xc)` 经 `/dev/null` fd 走 `ENOTTY` 路径返回 `0x26` 无崩溃（对比 r354 `0x929ce` SIGSEGV）。设备打开路径盘点：render minor `0x80–0xbf` 扫描 + driver 名 `pvr`/`mtgpu` 匹配 + `ioctl(0x40046445)` + `BridgeConnect`。r358 活体前置与验收判据已写出（只建连不提交 GPU 工作）。新增 `tests/test_umd_connection_layout.py`（6 项），反向验证通过；`check-offline` 400+299 全绿。会话未碰，freeze 继续。见 `reports/r357-umd-connection-linkage.md` + 证据。

- **r356 0x82:0xC 入库（离线，零硬件触碰）**：MUSAKICKGFX2 wire 结构入库（`mt_pvr_wire.h`，IN 268/OUT 12，5.2 DKMS 头实证类型尺寸，与 requirements 表 268/12 一致）；dispatch 接 observer 占位（解码打印后返 `-ENOTTY`，明确非执行，守 STATUS 红线）；门禁钉尺寸+7 偏移（`test_pvr_wire_sizes.py`），反向验证通过；`check-offline` 394+299 全绿，`make kernel` W=1 零警告。会话未碰，freeze 继续。见 `reports/r356-musakickgfx2-wire.md` + 偏移表。

- **r355 DDK2 render backend 缺口盘点（离线，零硬件触碰）**：桥侧 dispatch 三分法——已真实实现（SRVCORE/SYNC/MM、0x88:0x4 翻译、TQX fire、MUSAKICKGFX5 schema 108B）；accept-and-log 空桩（0x82:0x14、0x89:0xa、0x82:0x12/0x88:0x5、0x89:0x8/0x9）；缺失（0x82:0xC、TA firmware 提交通道、per-file VM/BO、UMD 真实建连）。新发现：r354 证据 `0x92930(rdi=0x6000,rsi=0x82,rdx=0xc)` 表明 UMD TA 路径内实际发出 `0x82:0xC`=MUSAKICKGFX2（5.2 头对照），桥侧无定义走 `-ENOTTY`；STATUS 口径修正：0x82:0xC 从 S4 边界升级为下一个具体桥目标，0x81:0x5 维持边界。需求 R1–R7 与 r356+ 分轮分解（r356=0x82:0xC wire 入库；r357=UMD 真实建连 recon；r358=活体观察需批准；r359=TA 通道设计；r360=0x82:0x14 翻译设计）。语料 SHA 对版；`PVRSRVBridgeCall` ioctl 号 `0xc0206440` 与桥侧 static_assert 同值。会话未碰，freeze 继续。见 `reports/r355-ddk2-render-backend-gaps.md`。
- **r354 T2-g（离线 fabricated，零硬件触碰）**：`0x929ce`（`mov (%rax),%edi`，`rax=fault_addr=0x6000`）SIGSEGV 定性为 fabricated artifact——`GetSrvHandle @ 0x3c1c0`（`rdi ? *(uint64_t*)rdi : 0`）从结构体首 qword 读出句柄值 `0x6000` 并返回，经 `0x79733→0x7a30b→0x36ec0→0x37111→0x92930(rdi=0x6000,rsi=0x82,rdx=0xc)`，在 `0x929ce` 被当作指针解引用以取 ioctl fd（`_IOWR('d',64,32)`）；真实路径下该字段必为有效指针（官方驱动真机正常），harness 未做完整 PVRSRV 建连所致。教训：pending 断点实际落在 `RGXKickTA+17`，`pc-0x7afd0` 误算 base 差 `0x11`；改由 `info proc mappings` 取 base。会话未碰，freeze 继续。见 `reports/r354-929ce-segv-artifact.md` + 三证据。
- **r353 T2-f（离线 fabricated，零硬件触碰）**：回填 b10 真描述子到槽 0（`0x79c92` 处 `$rdx+0x48`，原 NULL）后，完整 `RGXKickTA` 路径上 `SyncPrimRef` 两次返回 0，`SubmitTA` 越过检查未跳 `0x7ada7`；T2 核心问题闭合。`SyncPrimRef` 成功后下游 `0x929ce` 处新 SIGSEGV（T2-g 起点）。方法：GDB 从头 + `disable-randomization off` 绕堆布局崩溃；纠正nohup致崩误判实为 harness 引号 bug。会话未碰，freeze 继续。见 `reports/r353-syncref-e2e-zero.md` + 单证据。
- **r352 T2-e（离线 fabricated，零硬件触碰）**：b10 描述子直接验证通过——`SyncPrimRef(b10@0)` 返回 0，`SyncPrimRef(NULL)` 返回 3（基线）；描述子 `+8=1` 符合要求。`r14+0x18` 链静态定位：r14=RGXKickTA `rbp-0x170` 栈结构体经 PrepareTA 原样传给 SubmitTA，`+0x18` 由 PrepareTA 写入；具体来源 buffer 未实测（GDB 完整 mapA 下堆布局敏感 SIGSEGV）。T2-f 为来源实测或 harness 层回填。会话未碰，freeze 继续。见 `reports/r352-syncref-backfill.md` + 双证据。
- **r351 T2-d（离线 fabricated，零硬件触碰）**：描述子选中步骤定位——`0x79c92: mov 0x48(%rdx),%rdi`，`rdx=rbx+208*i`，`rbx=*(*(r14+0x18)+0x30)`，`i=*(rbx+0x24)`；fabricated 下 `i=0`，槽0 `+0x48`=NULL，故 `SyncPrimRef` 报 3。GDB 链式复核 `*(*(r14+0x18)+0x30)==base` 成立，断点单次命中（无循环）。T2-e 为 `r14+0x18` 对象来源与 b10 描述子回填槽0。会话未碰，freeze 继续。见 `reports/r351-submitta-desc-select.md` + 双证据。
- **r350 T2-c（离线 fabricated，零硬件触碰）**：`SyncPrimRef` 判空描述子即 3，传入确为 NULL；真 handle 已备，回填位置未中。T2-d 找描述子选中步骤。会话未碰，freeze 继续。见 `reports/r350-syncref-wants-desc.md` + 双证据；脚本增量单提交。
- **r349 T2-b（离线 fabricated，零硬件触碰）**：5 被调者全良性，`SyncPrimRef` 首报 3（`INVALID_PARAMS`，需真 handle）；T2-c 回填 tuple。会话未碰，freeze 继续。见 `reports/r349-submitta-syncprimref-3.md`（无新增证据文件）。
- **r348 T2-a（离线 fabricated，零硬件触碰）**：`RGXKickTA` 双映射 `-> 3`；`PRET=0/SURET=3`；`0x82:0x14` 未发出。T2-b 为 SubmitTA 归因。会话未碰，freeze 继续。见 `reports/r348-ta-fabricated-3.md` + 三证据。
- **r347 T1 关闭（离线，零硬件触碰）**：`EnQueue` 纯入队；0x82 静态 18 wrapper、无 `0xC`（S4 改述为无生产者）；T2 打 `0x14`。本轮两次 `/tmp` 中间产物违规已删。会话未碰，freeze 继续。见 `reports/r347-enqueue-no-bridge-cmd.md`。
- **r346 TA 阶梯（离线，零硬件触碰）**：`RGXKickTA` 入口链定锤（`+0x30` 守卫 + `PrepareTA@0x78800` + `SubmitTA@0x796b0`）；缺 producer/桥口/执行；T1 为 `SubmitTADataEnQueue` 桥命令归属。会话未碰，freeze 继续。见 `reports/r346-ta-bringup-ladder.md`。
- **r345 app 未描述 surface（离线，零硬件触碰）**：copy 提交序列无 surface 调用，`CreateCCB` 只做 calloc；r333–r345 因果链闭合，缺口在 vendor 测试程序 setup（开放项记报告）。copy 线关账，待拍板。会话未碰，freeze 继续。见 `reports/r345-app-never-describes-surface.md`。
- **r344 分发门（离线，零硬件触碰）**：`QueueTransferNew` 按 `features+0x54` 分发（`>1→TQJobSubmit`，`≤1→legacy`）；`rdx+8` 被 r342×r338 活体互证；`=2`/默认行为分裂得解。会话未碰，freeze 继续。见 `reports/r344-queue-dispatch-gate.md`。
- **r343 修正 r342（离线，零硬件触碰）**：rdx 缓冲 `[0,0x820)` 由 `rep stos` 清零，`+0x820` 起的非零值是残留栈，撤回“app 填入”解读；反汇编闭合。会话未碰，freeze 继续。见 `reports/r343-buffer-zeroed-tail-stale.md`。
- **r342 app 入参（批准执行）**：`=2` 窗口断 app `0x4026`——rdx 缓冲 `+0x820/+0x828/+0x838` 非零（栈指针），计数槽未变；收回 r341 “清零后无回填”。默认回 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r342-app-args-live.md` + 双证据。
- **r341 尾跳调用方（批准执行）**：`=2` 窗口返回地址点名——app `0x4026` 调 QueueTransferNew，`+0x46` 尾跳进 JobSubmit；`bt` 静默根因亦明。默认回 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r341-tailcall-caller.md` + 双证据。

- **r340 bt 通用静默（批准执行）**：`=2` 窗口 JobSubmit 入口 `bt` 同样零输出，改走 `x/gx $rsp`；app 进 transfer API，TQ 经指针到达。默认回 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r340-bt-silent-caller-ptr.md` + 双证据。

- **r339 ctx 来自调用方（离线）**：`*(job+0x10)` 调用前已存在，序言零写 `+0x58`；建表责任在调用方，链条终版。会话未碰。见 `reports/r339-ctx-from-caller.md`。

- **r338 空壳定锤（批准执行）**：`=2` 窗口 GDB 六点快照——JobSubmit 入口链尚空，空壳建于序言；copy 表从未被建，转立项。默认回 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r338-empty-shell-verdict.md` + 双证据。

- **r337 零值胎里带来（批准执行）**：`=2` 窗口 GDB 四入口快照——ctx 链在 BlitInit 入口已全链接且 `cnt=0`，生产者在上游；默认回 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r337-zero-at-birth.md` + 双证据。

- **r336 同路径无人写（批准执行）**：`=2` 窗口 GDB 单发——`+0x3320` 入口锚定 + 计数槽写观察零命中 + 分发 `0/1/0`；默认回 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r336-nowrite-inpath.md` + 双证据。

- **r335 调用链静态闭合（离线）**：多入口簇定锤，TQ 经 `+0x3320=0x889c0`（`0x601dd` 直调→分发→`+0x2ff0`→abort 循环）；entry 零命中得解，纠算术一处。会话未碰。见 `reports/r335-callchain-static.md`。

- **r334 空表断言（批准执行）**：`=2` 窗口 GDB 单发 copy tq-perf——release 内断言首轮即中（`ebx=0/edx=0`，容量槽 0）；默认回 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r334-empty-table-assert.md` + 双证据。

- **r333 出参判决（批准执行）**：`=2` 窗口 GDB 单发 copy tq-perf——CF 一次命中（0/1/0/0）→ abort 点 `[r8]` 仍=1（mismatch 解读死亡）；默认回 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r333-cf-slot-verdict.md` + 双证据。

- **r332 重建后 freeze（批准执行）**：冷启动后新 trial `20261008T025100Z-c85ff8c5`（Guest/FW 2/2 pinned，固件 sha 不变，probe ref 1）→ 默认桥（`card1`/`renderD128`，ref 0）→ L3 全绿；cold 因设备已干净被拒（非缺口）；dmesg 干净。在载桥为 r331 新鲜构建。**Freeze 生效。**见 `reports/r332-session-rebuild.md`。

- **r331 拼写收尾（离线，零硬件触碰）**：r332 草稿闭合——双 `#else` 修复 + 9 处裸 `pr_info` 转 `mt_gpu_vm_log`（三头复用）；门禁 8 项（含反向）；`check-offline` 394+299 全绿，`make kernel` W=1 零警告，`make check` 全绿（HEAD 上原红）；bootstrap 页表字节零变化（validation json 零 diff）。开工时会话已随冷启动消失（仅 `card0`）。见 `reports/r331-userspace-spellings.md`。

- **r330 桌面全清（用户指令）**：exe 精确匹配清桌面树 10 进程（serve 保留，会话存续）；renderD128 零持有，bridge ref 0——**现为天然重载窗口**。见 `reports/r330-desktop-cleared.md`。

- **r329 菜单 flag（用户指令）**：`--disable-gpu` 已入菜单覆盖层并验证生效，但 renderD128 照持（flag 与主进程占用无关，两次实锤）；GDB 函数注入 UI 事故备忘。bridge 未动，refs 1/1。见 `reports/r329-desktop-nogpu.md`。

- **r328 字段落定（批准执行）**：`type=0/count=1/flags=0/a8=0` 两轮一致；调用点证明返回值被忽略、`[r8]` 系出参；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r328-checkfences-fields.md` + 双证据。
- **r327 切入（批准执行）**：真入口对齐解码 type/count 开关；dprintf 转义教训；L3 双绿，窗口零新增 WARN。见 `reports/r327-checkfences-disasm.md`。

- **r326 EOT 证伪（批准执行）**：先 PASS blit 再 tq-perf，仍同形 abort——完成态不是钥匙，abort 条件自带；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r326-fill-then-copy.md` + 双 `.jsonl`/stdout。

- **r325 双空跑（批准执行）**：嵌套 `finish` 静默失败；`LookUpEOT` 无 `ret`（尾跳风格）；返值改 core 出参/行为判据；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r325-nested-miss.md` + 三 `.txt`/`.jsonl`。

- **r324 未遂（批准执行）**：finish 版脚本空跑（pending 断点未命中）；r323“全返回”收敛为“全进入”；返值改两步走（裸断停机 + 第二会话 finish）。拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r324-lookupret-miss.md` + `.txt`。

- **r323 setup 全过（批准执行）**：三元组全进入全返回（含参数），abort 在其下游、submit 未达；dprintf 文件脚本法定稿；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r323-setup-trace.md` + 双 `.txt`。

- **r322 桩读参（批准执行）**：abort 调用点三命中；`rdi` 系堆 job 结构（指针×5 + 计数 + cookie）；abort 在 `RGXTDMSubmit` 内自杀（尾跳）；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r322-abort-stub-params.md` + 双 `.txt`。

- **r321 尾跳定锤（离线）**：abort 经 file `0x88650` 尾跳直达 abort 调用点（非直接调用；解释 core 两帧栈 + r320 未命中）；helper 为静态函数名不可考；r322 断调用点读参。无代码改动。见 `reports/r321-abort-tailjump.md`。

- **r320 机制收官（批准执行）**：batch 符号教训（`start` 需 `main` 符号；绝对地址断点经 python 现算可用）；abort 点寄存器已破坏；栈取证得 destination-magic + 维度对（组装期 abort）。拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r320-abort-mechanics.md` + 双 `.txt`。

- **r319 三发全同（批准执行）**：sysmem/小几何/对照三发同形 abort（134/8200 行/101 全零/末 map）——abort 与配置无关；abort 桩已定位（file `0x2c8c0`，`TQ_BlitInit→…→ReleaseCPUMapping` 后）；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r319-tqperf-matrix.md` + 三 `.jsonl`/stdout。

- **r318 copy RE（离线）**：abort 系断言式自杀（setup 深水区），bridge 全 0 无罪；候选按验证成本排序（sysmem/小几何/GDB/全反汇编）；无代码改动。见 `reports/r318-copy-abort-re.md`。

- **r317 copy 侦察（批准执行）**：tq-perf 单发止于 `TQJobSubmit` 内 abort（101 调用全零；r162 复现；core 入库）；copy/TA 均需 producer 级 recon，非单窗口工程。拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r317-tqperf-recon.md` + `.jsonl`/`.bin`。

- **r316 Test PASS（批准执行）**：五开 + `+0` 落池，UMD `Output matches source / Test PASS (exit=0)`——真实绘制全链条打通，STATUS #1 落定。拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r316-test-pass.md` + `.jsonl`/stdout。
- **r315 池基修正（离线）**：落池基址 HEAD→0（比对点名直接结论）+ 门禁改判；386 全绿，W=1 零警告。见 `reports/r315-zerobase.md`。
- **r314 点名（批准执行）**：比对 `dest+0` vs `source+3841` 全 5MB（rcx/rsi/r13d 活体）；L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r314-compare-addrs.md` + `.txt`/`.jsonl`。

- **r313 几何落定（批准执行）**：源池 solid 全覆盖实锤（`first=3841 last=5246717` 恰为像素体）；颜色/归属/执行皆对，比对输入只差点名（r314 GDB 断比对循环读双方地址 + 字节数）。拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r313-pattern-geometry.md` + `.jsonl`/stdout。

- **r312 图案边界（离线）**：首/末非零字偏移 + 值打印（autorect 已回滚，读数先行）；门禁全绿。未加载。
- **r311 形态读取（批准执行）**：`1019` 全零/`101b` 单字节/`1032` 双值；blit 判决随暂存区丢失（备忘）；L3 双绿，窗口零新增 WARN。
- **r310 内容形态（离线）**：distinct 像素计数打印；门禁全绿。未加载。
- **r309 颜色双发（批准执行）**：RED/GREEN 覆盖均排除；拆桥 + L3 双绿，窗口零新增 WARN。
- **r308 颜色覆盖（离线）**：`translate_fire_color` 参数；门禁全绿。未加载。
- **r307 定向首验（批准执行）**：`pristine override pool=0x1019` 生效但仍 FAIL；`ccbref: total=10`；拆桥 + L3 双绿，窗口零新增 WARN。
- **r306 VA 普查 + pristine（离线）**：ccbref 映射 + pristine 同几何优先；门禁全绿。未加载。
- **r305 magic 普查（批准执行）**：六魔数全零命中，真实布局与构建器无交集；拆桥 + L3 双绿，窗口零新增 WARN。

- **r304 CCB 扫描（离线）**：官方树无执行逻辑可抄（OS 胶水 + 闭二进制，已持续 RE 采矿）；共享头目的扫描 + C 回环自测 + locate 定向；380+299 全绿，W=1 零警告。未加载。见 `reports/r304-ccb-scan.md`。
- **r303 归属反转（批准执行）**：三池同尺寸，nz=0/1/2621440——fire 在填源池（`0x1032` 图案完整），目的池（`0x1019`）恒零；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r303-pool-inventory.md` + `.jsonl`。

- **r301 落池首验（批准执行）**：五开两发——首发尾块 span bug（排序正确拒 bump）；复打 `fired=1 ... todst=1` + bump，UMD 仍像素 FAIL（候选错池/stride/错色，r302 离线判）；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r301-dstfire-live.md` + 双 `.jsonl`/双 stdout。
- **r300 落池设计（离线）**：work 验拷 + handler 等 fire 再 bump；门禁 +5；378+292 全绿，W=1 零警告。见 `reports/r300-dstfire-design.md`。

- **r299 越过 submit3（批准执行）**：诊断 `entry 1 sync=0x0`（NULL 填充）→ 双遍跳过修 → 复打 `update=2 first_sync=0x1029 first_val=1`，UMD 等待即过、止于像素比对（exit=1 干净）；剩余缺口 = fire 写 UMD 目的池。拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r299-bump-unblocks.md` + `.jsonl`。

- **r298 bump 被拒（批准执行）**：`=2` + bump 窗口首跑 `submit3 bump refused: -95`（某 update 柄无 CPU 可见内存），UMD 即时 abort（hang/abort 因果再证）；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r298-bump-refused.md` + `.jsonl`。

- **r296 三元组落定（批准执行）**：`SyncPrimWait` 入口 `rsi=0x174876e800==100000×1000000` 精确成立（100 秒双编码铁证），全进程仅调用一次；恢复经一次重开挡回后二次 10 秒内关账；L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r296-spw-entry.md` + `.txt`/`.jsonl`/`.dmesg`。

- **r295 参数未遂（批准执行）**：反汇编钉死 `SyncPrimWait` 有界等待 + 32B 同步表形状；fabricated 真 IN 得 `0x6005/0x6009` 复位；入口参未得（r296 以修正 batch 重抓）；暂存区丢文件备忘（gdb-args 幸存已入库）；L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r295-arggrab-partial.md` + `.txt`。

- **r294 spin 实锤（批准执行）**：hang 中 `R + wchan 0` + GDB 活体栈 `SyncPrimWait→sched_yield`，约 100s 后 `sutu_fail_if_errorI` 自杀 SIGABRT（core 已入库）；`=2` 纯 observe 照挂（hang 不需 tqx_ctx）；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r294-syncprimwait-live.md` + 双 `.jsonl`/`.bin`。

- **r293 hanging recon（离线）**：submit3 后零 syscall 系等完成信号（无 fence/无回写/零像素三重缺失）；r279 不定论收回；r294 以 wchan/stack/GDB 验 poll-vs-spin。见 `reports/r293-hang-recon.md`。
- **r292 双发全绿（批准执行）**：同 translator 内 `seq=1`/`seq=2` 背靠背 `fired=1 chunks=21 verified=1`，单发复位成立；CCB 第四/五样本 `nonzero=40`（轮值第 10/11 值）；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r292-double-fire.md` + 双 `.jsonl`/`.dmesg`。

- **r292 双发全绿（批准执行）**：同 translator 内 `seq=1`/`seq=2` 背靠背 `fired=1 chunks=21 verified=1`，单发复位成立；CCB 第四/五样本 `nonzero=40`（轮值第 10/11 值）；拆桥 + L3 双绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r292-double-fire.md` + 双 `.jsonl`/`.dmesg`。

- **r291 复现全绿（批准执行）**：第二窗口零干预全绿（停→三开→blit→fired→拆→默认→L3→拉回），UMD 矩形二次 `fired=1 chunks=21 verified=1`；CCB 第三样本 `nonzero=40`（轮值第 9 值 `dd 35`）；r290 自重启说已订正（用户手动重开）。窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r291-umdfire-repeat.md` + `.jsonl`/`.dmesg`。

- **r290 UMD 驱动 fire 首绿（批准执行）**：停桌面窗口 + 三开重载 + 真实 blit，UMD 矩形（1280×1024）`fired=1 chunks=21 verified=1 bad=0/1310720`；恢复曲折（用户手动重开桌面致 rmmod 被拒，r291 已订正非自重启，手动补恢复关账，probe 31→1）；L3 全绿，窗口零新增 WARN。**Freeze 已恢复。**见 `reports/r290-umdriven-fire-live.md` + `.jsonl`/`.dmesg`。
- **r289 超限拒绝（批准执行）**：3840×2160（128 块）`run` 即 `-E2BIG`（`fired=0`，零提交零像素触碰）；拆模块干净，窗口零新增 WARN。模块正反分支活体全覆盖。**bridge 未碰，freeze 继续。**见 `reports/r289-e2big-live.md` + `.dmesg`。
- **r288 合并决策（离线）**：fire 保持独立（正式工具）+ 桥侧留 UMD 路径；桥 r280 fire 记流水线自阻塞 defect（合流前须串行移植）；holder 解锁程序明确（stop service，需用户协调窗口）。无代码改动。见 `reports/r288-merge-decision.md`。
- **r287 上限边界全绿（批准执行）**：4096×1024（16 行/块恰 64 块）`fired=1 chunks=64 verified=1 bad=0/4194304`；拆模块干净，窗口零新增 WARN。**bridge 未碰，freeze 继续。**见 `reports/r287-capedge-live.md` + `.dmesg`。
- **r286 legacy 基线（无重载）**：当前构建+会话下真实 blit 止于 `0x89:0x0` → -25（101 调用仅此一非零，与 r244 同形）；refs 不变，窗口零新增 WARN。**freeze 继续。**见 `reports/r286-legacy-baseline.md` + `.jsonl`。
- **r285 soak 全绿（批准执行）**：5 轮装/打/卸轮轮 `fired=Y verified=Y chunks=21`，ref 全对称；窗口零新增 WARN；无代码改动。**bridge 未碰，freeze 继续。**见 `reports/r285-soak-live.md` + `.dmesg`。
- **r284 大矩形通用性（批准执行）**：1920×1080/`0xff00ff00` 下 `fired=1 chunks=32 verified=1 bad=0/2073600`，与离线预言逐项一致；拆模块干净，窗口零新增 WARN。**bridge 未碰，freeze 继续。**见 `reports/r284-bigrect-live.md` + `.dmesg`。
- **r283 独立模块真发射全绿（批准执行）**：自有节点 bring-up（`prepared=1` + `slices: ready`）后 21 块 fill fence 全 signal、1310720 像素逐块验过（`fired=1 chunks=21 verified=1 bad=0`）；活体三处反馈（completed 门删除/串行流/verify 设 upload_dev）已修并钉入门禁；拆模块干净，窗口零新增 WARN。**Freeze 已恢复（bridge 未碰）。**见 `reports/r283-fire-live-verified.md` + `.dmesg`。
- **r282 fire 独立模块（离线，零硬件触碰）**：新模块 `mt_live_tqx_fire` 自有 render 节点 + 自有 bring-up/分块 fire（21 块/21 fence/验尾块），直连 probe 会话，不碰 bridge；门禁 9 项（含反向）；`check-offline` 363+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r282-fire-module-offline.md`。
- **r281 分块 fire 活体被挡回（批准执行，未触硬件）**：`renderD128` 被会话桌面自身（PID 60651）持有，bridge ref 1，`rmmod` 被拒即停；refs 不变，本轮窗口零新增 WARN。r280 待 holder 释放后重跑。见 `reports/r281-chunkfire-blocked.md`。
- **r280 fire 分块循环（离线，零硬件触碰）**：收尾盘内半成品（struct 数组化而 submit/teardown 仍单 fence），全帧切 21 块/21 fence/验尾块；门禁 +3（含反向）；`check-offline` 354+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r280-fire-chunks-offline.md`。
- **r279 64 页绕行验证（批准执行）**：Chrome 关闭后重载验证通过，slices ready + tqx-ctx ready 全现；blit hanging 系 UMD 行为（可 rmmod，对称归零）。拆桥干净，默认 + L3 全绿；本轮窗口零 WARN。见 `reports/r279-space64-live.md` + `.jsonl`。
- **r278 space 缩小绕行（离线，零硬件触碰）**：space 2112→64 页（旧上限内）+ scratch 8MB→256KB；门禁改判；`make kernel` 零警告；`check-offline` 全绿。Chrome 被动持有挡 rmmod（后用户关闭）。见 `reports/r278-space-shrink.md`。

- **r277 bind 黑盒未打开（批准执行）**：bind 细分打印全无新行，-22 仍在 bind_boot_shared 内；新打印行缺失未解。拆桥干净，默认 + L3 全绿。见 `reports/r277-bind-blackbox.md`。
- **r276 WARNING 修复验证（批准执行）**：真条件是 prepare 失败路径 teardown；INIT 前移后失败路径零 WARNING（fail_at 行号+6 自证）；门禁 +1；`check-offline` 351+292 全绿；`make kernel` 零警告。拆桥干净，默认 + L3 全绿。见 `reports/r276-warning-fixed.md`。
- **r275 fail_at 定位 + WARNING 修复（批准执行）**：`failed at line 1856` 直指 bind_boot_shared；首个内核 WARNING（cancel 未 INIT work）已修未复验；`check-offline` 350+292 全绿；`make kernel` 零警告。拆桥干净，默认 + L3 全绿。见 `reports/r275-failat-warning.md`。
- **r274 fire 活体失败（批准执行）**：三开 + blit，DM prepare 先倒（-22 重现，slices/fire 未达）；blit 即时 134，无 D 态；refs 自归。拆桥干净，默认 + L3 全绿。见 `reports/r274-fire-blocked.md`。
- **r273 收敛收官审计（零硬件触碰）**：全仓库残留裁决，生产代码零散落；r269–r273 收敛工作关闭。见 `reports/r273-convergence-audit.md`。
- **r272 槽位号与驱动名收敛（离线，零硬件触碰）**：20 处 slot + 22 处驱动名合一（单次使用不碰；中途漏 3 文件被残留 grep 抓获）；旧门禁改判；门禁更新（含反向）；`check-offline` 347+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r272-slotname-refactor.md`。
- **r271 DID/VID 收敛（离线，零硬件触碰）**：17 文件 guard 合一 helper（宽松 5 处保留）；门禁更新（含反向）；`check-offline` 347+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r271-didvid-refactor.md`。
- **r270 误读反思（零硬件触碰）**：三层概念混淆 + 画像零入库是根因；横向排查（显示/编解码/mpc/Host版全漏）；画像已入库 + AGENTS 检查单。见 `reports/r270-quota-lesson.md`。
- **硬件画像（r270；trial `20261007T131341Z-3b9ae877`，`decode-device-info.py` 解 `info_raw`，版式源 `mtkm64.sys`）**：`vm_memory_size_bytes=1073741824`（**1GiB** Guest 配额；此前 90MB 误读系固件启动池，16G 系 PCI 窗口）；`bar2_actual=1124073472`；6 段（80M@`0x782000000` + 926M@`0x8c000000` + 64M@`0x77dfef000` + 200M + 80M + 2M@`0x43000000`）；version=2/osid=1/flags=`0x3d1`；显示 `2560x1600`、编解码 3/5 实例、mpc=1；connection 另有 `host_version=0x105000500070002`、`render_ready=0`。会话重建必须重解并 diff，变化即告警（r270 教训）。
- **r269 Guest 地址收敛（离线，零硬件触碰）**：1GiB 配额纠正（info 解码；纠正 90MB 误读）+ BAR2/SEG5 字面量收宏（堆表保持设计）；门禁 2 项（含反向）；`check-offline` 346+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r269-guest-addrs.md`。
- **r268 space 上限修复（离线，零硬件触碰）**：64→2112 页 + keys 栈改堆（goto-out 重构）；fire 活体失败根因（-EINVAL 即 -22）；门禁上限断言（含反向）；`check-offline` 344+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r268-table-budget.md`。
- **r267 fire 函数离线实现（零硬件触碰）**：scratch 8MB（space 2112）+ locate helper（digest 不变）+ fire/submit + workqueue 回读 + param 门；teardown 首 cancel。门禁 fire 8 项 + 双改判（含反向）；`check-offline` 343+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r267-fire-impl.md`。
- **r266 slices 重验通过（批准执行）**：锁序修复生效，`tqx slices: ready cores=1` 全现，无死锁无 D 态；blit hanging 系 UMD 行为（可 rmmod，probe 30→1 对称）。拆桥干净，默认 + L3 全绿。见 `reports/r266-slices-verified.md` + `.jsonl`。
- **r265 锁序修复（离线，零硬件触碰）**：trial_lock 分段放/取，slices 移出嵌套（translator_lock 防重入，走查三调用点）；门禁顺序断言（含反向）；`check-offline` 335+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r265-lockorder-fix.md`。
- **r264 重启后重建（批准执行）**：cold 0/1 双 clean → 新 trial `20261007T131341Z-3b9ae877`（Guest/FW 2/2 pinned，probe ref 1）→ 默认桥（`card1`/`renderD128`，ref 0）→ L3 全绿；dmesg 干净。在载桥是 r263 含死锁构建（默认参数下休眠）。**Freeze 即刻生效。**见 `reports/r264-session-rebuild.md`。
- **r263 后重启（恢复中）**：机器已重启，无模块，`/dev/dri` 仅 `card0`；死锁随重启清除。r263 代码在树内未加载，会话待重建。见 `reports/r263-slices-deadlock.md`。
- **r263 slices 死锁待重启（批准执行）**：调用可达证实后第二轮卡死 buffers->lock（D 态 blit，进程栈 + sysrq 双实锤；trial_lock→buffers.lock AB-BA）；rmmod 被拒，待重启。见 `reports/r263-slices-deadlock.md`。
- **r262 slices 活体未达预期（批准执行）**：新构建上机 + blit 后 `tqx-ctx: ready` 正常，但 slices 零执行零打印（已排除在载≠盘内/调用点错/dmesg 丢；调用未到达待查）。其余正常，拆桥干净，默认 + L3 全绿。见 `reports/r262-slices-noop.md`。
- **r261 bring-up 补 pool slices（离线，零硬件触碰）**：copy prepare 填 slices（非致命）+ 读镜像 + 先释放后销毁；门禁 6 项（含反向）；`check-offline` 333+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r261-tqx-slices.md`。
- **r260 ck 下 legacy blit（批准执行）**：同样止于 `0x89:0x0` → -25（ck 不干扰 `0x89` 路径）；trace 落硬盘暂存区后已清空。拆桥干净，默认 + L3 全绿。见 `reports/r260-ck-blit.md`。
- **r259 r210 配方可复现性审计（零硬件触碰）**：无完整 harness 命令归档，缺 UMD 对象指针来源，不可直接复现；最小 GFX 命令 GDB 下返回 3（r194 复现成功）。暂存区已清空。见 `reports/r259-gfx-repro-audit.md`。
- **r258 硬盘暂存区流程验证（批准执行）**：AGENTS.md §5 改判落地；`/tmp/opencode/umda/` 4.2M 清零；新流程 ping/rung8 全过（trace 落硬盘）；暂存区已清空。见 `reports/r258-disk-traces.md`。
- **r257 RGXKickGfx 签名恢复（零硬件触碰）**：6 参数用途 + 调用链（objdump 实锤：rdi=render ctx，rsi=kickA，rdx=b24，rcx=kickB，r8=b25，r9=栈参）；harness 重建第一步；附 /tmp 用途调查。见 `reports/r257-kickgfx-signature.md`。
- **r256 TQX 真发射路径盘点（零硬件触碰）**：链条闭合（live_3d_drm 模板可照抄），唯一缺件是 bring-up 补 pool slices；路由已对；锁无障碍；destination 借用 + 回读方案齐备。三步立项。见 `reports/r256-tqx-fire-path.md`。
- **r255 短预算 10 轮 soak（批准执行）**：同窗口（wait 100ms）10/10 通过，0.12ms/轮；tag/fence 无跳号（至 79/80）；零 WARN。拆桥干净，默认 + L3 全绿。见 `reports/r255-tinybudget-soak.md`。
- **r254 极小预算失配腿（批准执行）**：wait 100ms 下 0.118s 后 UMD 37；预算维度全覆盖（5s/1s/100ms）；无 marker。同窗口续跑 soak。见 `reports/r254-tinymismatch.md` + `.jsonl`。
- **r253 极小预算匹配腿（批准执行）**：wait 100ms 下预置匹配 45ms 即过（fence=59/60）；预算下限安全（失配腿在 100ms 下未跑）。拆桥干净，默认 + L3 全绿。见 `reports/r253-tinybudget-match.md`。
- **r252 GDB 监督 L4 闭环（批准执行）**：rung6/rung8 全过（叠加 r245，新会话 L4 全级成立）；默认桥未重载。见 `reports/r252-gdb-l4-closed.md`。
- **r251 UMD standalone flake 现状（批准执行）**：render 路径 10/10 崩 + GDB 全过（r167 翻版）；flake 率演进 4/16→11/11→6/6→10/10；node probe 偶发 2 failing（重跑即过）。UMD 链 soak 不可行。见 `reports/r251-umd-flake-status.md`。
- **r250 observer 在 tqx_ctx 桥下回归（批准执行）**：`=2` + tqx_ctx 下 23 ok + PASS，CCB 行一致；observer × 5 配置正交补完。拆桥干净，默认恢复。见 `reports/r250-tqxctx-observe-regression.md`。
- **r249 observer 在 transfer 桥下回归（批准执行）**：`=2` + transfer 下 24 步全过（含负向双测），CCB 行一致；开关正交补格。拆桥干净，默认 + L3 全绿。无代码改动。见 `reports/r249-transfer-observe-regression.md`。
- **r248 EBUSY 后重试（批准执行）**：循环并发第 2 轮复现一胜一败；败者链单独重跑全过（0.18ms）；拒绝无副作用、可恢复。同窗口续跑 r249。见 `reports/r248-ebusy-retry.md`。
- **r247 translator 并发冲突（批准执行）**：双混合进程并行一胜一败，败者 submit 环节 `-EBUSY`（59µs 即拒，tag 被消费无行）；全局 markers 状态机不支持并发 submit。附带修 poll 假阳性 + 门禁更新。拆桥干净，默认 + L3 全绿。见 `reports/r247-translator-contention.md`。
- **r246 DDK2 param_1 三候选证伪（批准执行）**：`b14*`/`b5*`/conn DDK2 下全崩（同 RVA `0xa0b38`）；GDB 定三级链断裂点；语料确认 SetSyncPrim 系外部导出。工具边界，非桥缺口。拆桥干净，默认 + L3 全绿。见 `reports/r246-ddk2param-shape.md` + `.jsonl`。
- **r245 L4 legacy 部分（批准执行）**：rung7 compute 全过 exit 0；rung5/6/8 在 render create 处 standalone 6 连崩、GDB 全过（r167 翻版，桥无罪）。refs 不变，L3 全绿。见 `reports/r245-l4legacy-partial.md`。
- **r244 legacy 真实 blit（批准执行）**：默认桥上止于 `0x89:0x0` → -25，UMD 中止未到 submit；trace 8205 行入库；refs 不变，L3 全绿。见 `reports/r244-legacyblit-refused.md` + `.jsonl`。
- **r243 短预算 UMD 全链匹配（批准执行）**：wait 1s 下 `0x2:0xa` 预置 + kick 0.046s 即过（fence=40）；预算不影响 UMD 命中路径。拆桥干净，默认 + L3 全绿。见 `reports/r243-shortbudget-umdmatch.md` + `.jsonl`。
- **r242 DDK2 短预算失配（批准执行）**：`=2` + wait 1s 下 1.008s 后 UMD 37；major×预算双正交；无 marker。同窗口续跑 r243。见 `reports/r242-ddk2short-mismatch.md` + `.jsonl`。
- **r241 10 轮混合 soak（批准执行）**：同窗口 10/10 通过，0.10–0.20ms/轮（prepare 常驻复用，快约 300 倍）；tag=3..22、fence=20..39 无跳号；零 WARN。拆桥干净，默认 + L3 全绿。见 `reports/r241-soak-live.md`。
- **r240 短预算下匹配腿（批准执行）**：`translate_wait_ms=1000` 下预置匹配 36.6ms 即过（预算只限等待）；fence=18/19；同窗口续跑 soak。见 `reports/r240-shortbudget-match.md`。
- **r239 等待预算参数（批准执行）**：`translate_wait_ms=1000` 下失配 kick 1.008s 后 UMD 37（与 5s 的 5.007s 同构）；预算成比例生效；无 marker。拆桥干净，默认 + L3 全绿。无代码改动。见 `reports/r239-waitms-live.md`。
- **r238 定界活体验证（批准执行）**：超窗 2MiB → `-EINVAL`、野 index `0xFFFFFFFF` → `-ERANGE`，24 项全过；安全定界真实生效。工具加负向双测 + 门禁 +1；`check-offline` 327 Python OK。**Freeze 继续。**见 `reports/r238-bounds-live.md`。
- **r237 多文件并发（批准执行）**：默认桥上双 ping 并行双 PASS；dmesg 6 行齐、各自独立句柄，同 VA 零串扰；per-file 隔离成立。事后 L3 全绿。无代码改动。见 `reports/r237-concurrent-live.md`。
- **r236 fence fd poll（批准执行）**：混合 fire 回 0 后 poll 即时就绪（fence 已 signaled）；fence=16/17。工具加 poll 断言 + 门禁 +1；`check-offline` 326 Python OK。见 `reports/r236-fence-poll-live.md`。
- **r235 observer 在 translator 桥下回归（批准执行）**：同窗口（`=2` + `translate_kick=1`，未重载）22 步全过，CCB 行一致；开关正交证实。拆桥干净，默认 + L3 全绿。无代码改动。见 `reports/r235-ck-observe-regression.md`。
- **r234 DDK2 check 双腿（批准执行）**：`=2` + `translate_kick=1` 下零值 0.046s 过 / 失配 5.005s 后 UMD 37；`if (ncheck)` 在 DDK2 下同样真实；fence=15。双 trace 入库。**Freeze 已恢复。**见 `reports/r234-ddk2check-legs.md` + 双 `.jsonl`。
- **r233 多 update 条目（批准执行）**：update 数组 2 条目，混合 fire（check=2 + update=2）45.7ms 即过，改探第二槽 0.10ms 即过（update 循环全发布证实）；fence=13/14。门禁更新 + 双门禁反向；probe 对称，默认 + L3 全绿。**Freeze 已恢复。**见 `reports/r233-multi-update-live.md`。
- **r232 translator 混合 DDK2 回归（批准执行）**：`=2` + `translate_kick=1` 下混合工具 8 项全 ok（45.1ms 同构），fence=11/12；translator 与 major 正交证实。拆桥干净，默认 + L3 全绿。无代码改动。见 `reports/r232-major2-mixed-regression.md`。
- **r231 多条目混合 kick（批准执行）**：双 sync block 各预置一槽后，混合 fire（check=2 + update=1）44.8ms 即过，写回 probe 0.11ms 即过；dmesg `check=2 update=1 fence=9` → `check=1 update=0 fence=10`。工具双 PMR + 门禁更新；probe 对称，默认 + L3 全绿。**Freeze 已恢复。**见 `reports/r231-multi-check-live.md`。
- **r230 双开组合验证（批准执行）**：`=2` + transfer + tqx_ctx 同轮三行同现（observe/dry-run 预言一致/ready），两开关正交；附带第六个 `+0x40` 轮变值（`03 a6`）。**Freeze 已恢复。**见 `reports/r230-dual-param.md` + `.jsonl`。
- **r229 observer 全套 DDK2 回归（批准执行）**：`=2` 桥上 ping 全套 22 项全 ok，dmesg 三行与 legacy 逐项一致；dispatch 与 major 正交证实；legacy create 在 `=2` 下同样成功（附带）。拆桥干净，默认 + L3 全绿。**Freeze 已恢复。**见 `reports/r229-ddk2-observe-regression.md`。
- **r228 DDK2 `SetSyncPrim` 侦察（批准执行）**：`=2` 桥上该导出在 UMD 内 SIGSEGV（RVA `0xa0b38`，DDK2 分支把 connect 派生的 param_1 当 device 上下文解 `[0]` → 野读），`0x2:0xd` 从未发出；非桥缺口，合法形状待离线 recon。拆桥干净，默认 + L3 全绿。**Freeze 已恢复。**见 `reports/r228-ddk2set-segv.md` + `.jsonl`。
- **r227 混合 kick 活体验证（批准执行）**：预置 V7 后混合 fire（check+update）44.8ms 即过（与 r222 Leg1 同构），update 写回 probe 0.11ms 即过；dmesg `check=1 update=1 fence=7` → `check=1 update=0 fence=8`。工具升级三相 + 门禁更新；probe 对称，默认 + L3 全绿。**Freeze 已恢复。**见 `reports/r227-mixed-kick-live.md`。
- **r226 transfer dry-run 新会话复验（批准执行）**：`=2` + `translate_transfer=1`（tqx_ctx 保持 off）重载后真实 blit 报 `pool=0x1032/color=0xff0000ff/1280x1024/fnv=0xd893618ca42d3711`，与 r181 离线预言逐位一致。UMD 即时 SIGABRT 无 hanging；拆桥干净，桥恢复默认 + L3 全绿，dmesg 干净。附带第五个 `+0x40` 轮变值（`2a 9a`）。**Freeze 已恢复。**见 `reports/r226-dryrun-reverify.md` + `.jsonl`。
- **r225 UMD 生成 CCB 进真桥观察（批准执行）**：r210 字节 61 槽载入 0x4700 窗口再 fire，nonzero=107/FNV/head64 与离线预言全命中；22 步全 teardown，refs 不变。**Freeze 继续。**见 `reports/r225-ccb-observe-live.md`。
- **r224 observer 非零窗口活体验证（批准执行）**：零窗口 fire 后以 `0x2:0xa` 预置 5×u32 再 fire，桥报 nonzero=20/FNV/head 与离线预言逐项一致；13 步全 teardown，refs 不变。**Freeze 继续。**见 `reports/r224-observe-nonzero-live.md`。
- **r223 update 写回活体验证（批准执行）**：update-only fire 写 V=1 回 0，check kick 0.11ms 即时通过（时间即读回，无需 mmap）；dmesg 双行 fence=5/6；新工具 + 5 项门禁；probe 对称，默认 + L3 全绿。**Freeze 已恢复。**见 `reports/r223-update-writeback-live.md`。
- **r222 非零 kick 双腿闭环（批准执行）**：handler 搬到 `0x2:0xa`（新宏，生成头同名）+ `if (nupdate)`→`if (ncheck)`；门禁改判（syncprimset/render2/fn57/MAPPING）+ translator 新增 wait 门控断言；双重复位验证；`check-offline` 318+292 全绿；`make kernel` 零警告。活体（`translate_kick=1`）：Leg1 预置+匹配 0.045s 即过（fence=4）；Leg2 失配 5.007s 后 UMD 37（等待真实，无 marker）。probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。值语义真闭环。**Freeze 已恢复。**见 `reports/r222-nonzerokick-closed.md` + 双 `.jsonl`。
- **r221 非零 kick 活体发现（批准执行）**：UMD `SetSyncPrim` 实际发 `0x2:0xa`（objdump 实锤，Ghidra 伪 C 写错 fn id；r220 handler 挂错位置，下轮搬移）+ check-only 翻译不等 UFO 值（value=1 vs PMR=0 一次通过 fence=3；源码系 `if (nupdate)` 门控，r174 引入，疑笔误——r212/r213 只证明机械）。拆桥干净（probe 25→1），默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**见 `reports/r221-nonzerokick-findings.md` + `.jsonl`。
- **r220 SyncPrimSet 真写（离线，零硬件触碰）**：值语义链真卡点打通——`0x2:0x2` 从 stub 改真写（wrapper/生成头/活体三重互证 IN16；复用 translator 解析 + 定界写；无 fence/提交/wakeup；`0x2:0xd` 仍越界）。门禁 7 项新 + 改判 + MAPPING（含双重反向）；`check-offline` 317+292 全绿；`make kernel` 零警告。未加载，会话未碰。见 `reports/r220-syncprimset-write.md`。
- **r219 TQX bring-up 新会话复验（批准执行）**：`=2` + `translate_tqx_ctx=1`（transfer 干跑保持 off）重载后真实 blit 报 `tqx-ctx: ready`（无 `-22` 回归）；UMD 即时 SIGABRT 无 hanging；probe 1→28→1 对称归零；桥恢复默认 + L3 全绿，dmesg 干净。附带第四个 `+0x40` 轮变值（`60 70`）。**Freeze 已恢复。**见 `reports/r219-tqxbringup-reverify.md` + `.jsonl`。
- **r218 observer 全路径活体验证（批准执行）**：合法 envelope（真 render context + 真 PMR 窗口，阵列全 NULL）fire 回 0，dmesg 行标量全上报（check=1 update=1，零窗口零统计）；11 步全 teardown，refs 不变，dmesg 零新增。只证明全链不证明数组语义。**无重载，freeze 继续。**见 `reports/r218-observe-fullpath-live.md`。
- **r217 observer 分发活体验证（批准执行）**：新工具 `pvr_observe_ping` 发零填充 108B `0x82:0x14` 回 `-ENOENT`（路由到达），control `0x82:0x1f` 仍 `-ENOTTY`；fresh file 即关，refs 不变，dmesg 零新增。工具零警告构建 + 5 项门禁（含反向）；`check-offline` 307 Python OK。**会话未动，freeze 继续。**见 `reports/r217-observe-ping-live.md`。
- **r216 r215 新构建上机 + L3（批准执行）**：单桥重载（probe 未碰，装盘前验 strings + vermagic），节点仍 `renderD128`；node/smoke 全绿，refs 1/0，dmesg 零 WARNING/BUG/Oops。observer 已在载但尚无真实流量（parked，不是 proven）。**Freeze 已恢复。**见 `reports/r216-newbuild-reload.md`。
- **r215 `0x82:0x14` observer 离线实现（零硬件触碰）**：STATUS #2 缺口（r190）闭合一半：新增 `pvr_cmd_kickta3d5_observe`（r174 模式：108B 定界 + render 上下文鉴权 + 三重定界 + 标量上报回 0，不读嵌套指针、不执行、无 fence）；分发接 `case MT_PVR_FN_RGXKICKTA3D5`（wire.h 新宏）。门禁 7 项新 + fn 55→56（含反向掐断验证）；`check-offline` 302 Python + 292 C 全绿；`make kernel` W=1 零警告。未加载（在载桥仍旧构建，待批准窗口重载验证）。活体 GFX 方向已止损（无现成生产者）。见 `reports/r215-kickta3d5-observer.md`。
- **r214 真实绘制第二样本（批准执行）**：新会话真实 `musa_blit_test -device 0 -f -o`（`=2` 桥，零 fabrication）单次 `0x89:0xa`（8201 行 trace，与 r174 次轮同行数）；observe 行 VA/尺寸/res/PMR/39B/首偏移与 r174 全同，39 非零字节 37 跨会话一致、仅 `+0x40` 取第三值 `33 57`（执行级比对；源码核对 observe 回 0 无执行）。UMD 随后用户态 SIGABRT（r172 同例），内核干净；拆桥干净 + 默认恢复 + L3 全绿，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**见 `reports/r214-realblit-sample2.md` + `.jsonl`。
- **r213 DDK2 check-only 新会话复验（批准执行）**：`=2` + `translate_kick=1` 下六符号全 0，`0x82:0x12`/`0x88:0x5`/`0x88:0x4` 全 `ret=0`，dmesg `translated kick: check=1 update=0 tag=1 fence=2`（与 r149 逐字同形）。首跑复现 r144 `b5*` 间接缺失（CCB create 后用户态 SIGSEGV，桥侧干净回收），修正后即绿。拆桥干净、probe ref 25→1，桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。trace 249 行已入库。**Freeze 已恢复。**见 `reports/r213-ddk2checkonly-reverify.md`。
- **r212 check-only 新会话复验（批准执行）**：r211 新会话上 legacy `translate_kick=1` 首跑全绿（check 值取 r148 实测值 0）：六符号全 0、`0x88:0x4`（84/8）`ret=0`，dmesg `translated kick: check=1 update=0 tag=1 fence=1`（与 r148 同形）。拆桥 `unloaded cleanly`、probe ref 25→1；桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。trace 130 行已入库（`reports/r212-checkonly-reverify.jsonl`）。DDK2 check-only 已随 r213 复验通过。**Freeze 已恢复。**见 `reports/r212-checkonly-reverify.md`。
- **r211 活体会話重建（批准执行）**：本机重启进 `6.12.111`，旧 r166 会话消失；`cold_disconnect finish=0/1` 均 rings idle、`guest=0 firmware=1`、双 clean rmmod；`fresh-trial.py --run --runtime-context` rc=0，新 trial `20261007T040408Z-f3fb55af`（firmware sha `35d40f75…`，Guest/FW `2/2` pinned，probe ref 1）；`mt_pvr_bridge` 默认参数在载（`card1`/`renderD128`，ref 0）；L3 全绿（node 0 failing/0 mismatch，dma smoke PASS refs 平衡，无 GPU 提交）；dmesg 无新增 WARN/BUG/Oops。**Freeze 即刻生效。**见 `reports/r211-session-rebuild.md`。
- **r210 fabricated GFX CCB 字节已捕获（零硬件触碰）**：从含 `musa.ini` 的工作目录重放 SHA 对版 UMD，构造非空 check/update sync tuple 后 `RGXKickGfx` 返回 0；GDB 见 check/update helper 均返回 0，trace seq 125 发出 `0x82:0x14`（IN108/OUT4、VA `0x8000023000`、size `0x4700`、check/update counts=1）。r209 shim 将 18,176 字节 backing 落盘，SHA `faa93985aa6b3f65df66641ac73f25020a66fca7af6cdece3b9e88c3a315dae7`，107 字节非零。仅证明 UMD fabricated producer 的原始输出可取回，不证明 Guest handler 或 GPU 执行。证据：`reports/r210-gfx-ccb-capture.md`、`.bin`、`.jsonl`。
- **r209 fabricated GFX CCB 捕获能力（零硬件触碰）**：`umd_bridge_shim` 在 shared PMR 模式下按 5.2 schema 解析 `0x82:0x14` submission VA/size；显式设置 `UMD_CCB_DUMP_DIR` 才会将可解析 PMR backing 原始字节以 0600 文件落盘（16 MiB 上限）。合成测试覆盖 offsets/backing/miss/opt-in；size offset 注错可被测试捕获。295 Python（1 skip）+292 C 全绿。既有 r203 UMD 请求解码为 VA `0x8000023000`/`0x4700`，但真实生成 CCB 字节尚未捕获，fake shim 仍不执行。见 `reports/r209-kickgfx-ccb-capture.md`。
- **r208 DDK2 render backend 边界（零硬件触碰）**：`mt_bo_system_borrow()` 可将 PMR GPU page vector 包成设备 store 的 borrowed BO，但当前 per-file GPU VM/PMR facade 是 CPU-only store，且 borrowed handle 需要稳定 memory descriptor。真实接线还需 per-file 上传 VM、execution process/render context、PMR VA 绑定、嵌套 sync/PMR 验证、CCB 资源闭包与 fence 完成；全局 translator 和 marker/TDM observer 不能代替。仅只读检查，未改码/未跑门禁。见 `reports/r208-ddk2-render-backend-boundary.md`。
- **r207 `0x82:0x14` wire foundation（零硬件触碰）**：新增 108/4 结构和 flags/VA/size/ID/count 偏移静态断言，gate 对照 5.2 UMD size row；不接 dispatch。context 仅为 token，sync token 可关联 PMR；kick translator 只发固定 marker，TDM observer 不执行 CCB，因此真实 TA/3D backend 尚缺。`check-offline` 全绿，`make kernel` W=1 零警告。见 `reports/r207-kickta3d5-wire-foundation.md`。
- **r206 `MUSAKICKGFX5` schema 映射（零硬件触碰）**：hash 对版 DKMS 5.2 Host 生成头将 function +20 映射到 `0x82:0x14`；flags/VA/size/submissionID/check-update-PMR counts 与 r203 fabricated trace 的偏移和值吻合。服务 API 声明显示需要解析 update/check/PMR 嵌套数组；包没有 bridge handler 实现体。r207 已盘点 Guest handle 台账与当前执行路径，详见 r207。见 `reports/r206-kickgfx5-schema.md`。
- **r205 `RGXKICKTA3D5` 字段偏移（零硬件触碰）**：SHA 匹配 wrapper + r203 seq 123 trace 对上八个指针槽和 check/update/PMR count 偏移（1/1/0）；`0x48`–`0x5f` 尾部参数语义当时未定；r206 后由 5.2 Host schema 解释为 flags/VA/size/submissionID。Guest handler 消费语义仍待核对。见 `reports/r205-kickta3d5-field-offsets.md`。
- **r204 `RGXKICKTA3D5` ABI 边界（零硬件触碰）**：r203 fabricated trace/匹配 SHA UMD wrapper 确认 `0x82:0x14` 输入 108、输出 4；2.7.1 结构 96/4、2.3 Guest 无定义，5.2 Host schema 的 108/4 不证明 Guest handler 语义。先恢复逐字段契约并确认目标 handler，暂不直接复用 96B 结构。见 `reports/r204-kickta3d5-abi-boundary.md`。
- **r203 GFX update fabricated 干净返回（零硬件触碰）**：GDB watchpoint 把 r201 abort 定位到 `RGXKickGfx` RVA `0x7ee1a` 的 `rep movsq`：0x408 字节复制写越界，update-list size header `0x91→0x1151`。将 harness 的 `param_3`/`param_5` 缓冲均设为 0x410 后，header 保持 `0x91`，`0x82:0x14` 仍发出且 RGXKickGfx 返回 0、进程正常退出。shim 是 fabricated；真实 bridge handler/同步语义/GPU 执行未验证。r204 已确认 UMD/KMD 结构大小差异，逐字段契约仍待恢复。见 `reports/r203-gfx-update-clean.md` + trace。
- **r202 update-list 生命周期静态核对（零硬件触碰）**：`FUN_00178800` 按 count 分配 update-list block 并复制条目，成功路径在 fake bridge 返回后释放；SubmissionHead 另行拥有 region descriptor。helper-only 重放确认 descriptor 由 SubmissionDestroy 释放，但与 r201 abort 无直接因果证据；r203 已定位 abort 为缓冲越界写。见 `reports/r202-update-list-lifetime.md`。
- **r201 GFX allocator/update 动态复核（fabricated 离线）**：同 SHA UMD 默认 shim 下，GDB 证实 kick `+0x28` 目标 `+0x200` 与正常 render-context allocator 相同；正确 sync output-slot 下 CheckSync/UpdateSync helper 各有一项，update 项 `flag=2`、handle 非空，trace 发出 `0x82:0x14` IN108/OUT4。旧尺寸 harness 在后续输出复制中破坏 update-list chunk；r203 通过扩大缓冲证实可干净返回。见 `reports/r201-gfx-update-producer.md` + trace。
- **r200 render-context allocator 动态确认（fabricated 离线）**：默认 shim 下 connect/device/devmemctx/render context 全返 0；真实返回对象 `+0x200` allocator 与 `+0x318` SubmissionHead 均非空，桥 trace 117 行。r201 已将其 allocator 与 kick `+0x28` 目标动态对上。见 `reports/r200-renderctx-allocator.md` + trace。
- **r199 GFX allocator 首参来源（零硬件触碰；静态指令核对）**：`RGXKickGfx` 从 kick `+0x28` 指向对象的 `+0x200` 取 `SubmissionCmdGenerate` 首参；render-context 构造器把 `SubmissionBufAlloctor` 放在 context `+0x200`。`SubmissionHead` 是第二参来源的独立对象。r201 已动态确认该指针链一致。见 `reports/r199-gfx-submission-allocator-origin.md`。
- **r198 AppHint GFX 重放（fabricated 离线，无代码改动）**：临时 `musa.ini` 将 PerfCountEndCbID 设 0；不手动 poke，context `+0x24` 为 0，RGXKickGfx 越过 PrepareTA 后在 `SubmissionCmdGenerate` 因空首参 SIGSEGV。trace 109 行无 `0x82:0x14`。其首参 allocator 来源由 r199 定位到 kick `+0x28` 指向对象的 `+0x200`；具体空槽原因待动态确认。见 `reports/r198-gfx-apphint-replay.md` + trace。

- **r197 纠正 update 初始化解释（只读语料，无代码改动）**：render-context `+0x20/+0x24` 是 PerfCountStart/EndCbID；update-list count 属于 RGXPrepareTA 另行分配的对象。r198 实测 context `+0x24` 同时参与 PrepareTA 状态表索引，且可由 musa.ini 初始化；update helper 路径仍待核对 SubmissionCmdGenerate allocator 输入链。见 `reports/r197-correct-gfx-update-init.md`。

- **r196 RGXKickGfx update producer（fabricated 离线，无代码改动）**：进入 `SubmissionSetUpdateSyncPrim`，GDB 见 count=1 / flag=2；trace 发出 `0x82:0x14` IN108/OUT4，fake shim 与 RGXKickGfx 均返回 0。r197/r198 更正：手动 poke 覆盖 PerfCountStart/EndCbID；`+0x24` 同时作 context 状态表索引，不是 update-list count。r198 AppHint 重放越过 PrepareTA 后仍在 SubmissionCmdGenerate 崩溃；无真实 CCB 执行证据。见 `reports/r196-gfx-update-producer.md` + trace。

- **r195 flag&2 手塑（fabricated 离线，无代码改动）**：连接对象注入 flag=2 sync 条目后 `RGXKickTA → 3`，trace 无 kick ioctl；SHA 对版调用图证实 RGXKickTA 不调用 `SubmissionSetUpdateSyncPrim`，update caller 包括 RGXKickGfx/RGXMultiKickGfx。下步手塑 producer 层输入。见 `reports/r195-takick-flag2.md` + trace。

- **r194 psKickTA 手塑首轮（fabricated 离线，无代码改动）**：`RGXKickTA → 3` 干净退出；4 崩溃逐一定位；元素偏移纠偏；GDB 翻车 3 则已记。下步造 `flag&2`。见 `reports/r194-takick-shaping.md` + trace。

- **r193 psKickTA 构造（离线 recon，无代码改动）**：无铸造函数；锚点是真实 render 上下文（`+0xc`）；features `+0x54` 门内第二次出现。手塑回合可在 fabricated shim 下离线做。见 `reports/r193-taskickta-shape.md`。

- **r192 TA producer 收敛（离线 recon，无代码改动）**：`RGXKickTA` 等系导出符号，harness 直调可达 TA 链；前置 `psKickTA+0x30`（r85 的墙）；update 编组只活在提交链内。见 `reports/r192-ta-producer.md`。

- **r191 kill-while-busy 关账（批准执行；无重载）**：`rmmod` 被工具链持有挡回；GDB #104 处击杀（3 MAPs live），66→66 Δ0，无 D 态，L3 复绿。file_release 假设无活体支持；+65 未命名但边界收紧；7 活体轮零新增泄漏。见 `reports/r191-killbusy-d0.md` + trace。

- **r190 update 路径定位（离线 recon，无代码改动）**：非零 update 走 TA 链至 `0x82:0x14` RGXKickTA3D5（IN 108/OUT 4）；桥无此 handler，requirements 表亦 `CMD_LAST`。活体计划已列（observer + producer 待定）。见 `reports/r190-update-path-recon.md`。

- **r189 对象查找去重（零硬件触碰，含内核改动，未加载）**：`pvr_object_find` 收敛 9 处重复查找；map 删锁内重复 reservation 查找；connect/event/info/heap/pmr/open 走查无动作项。门禁 295+292，反向全过。见 `reports/r189-object-find.md`。

- **r188 保留项全抽取（零硬件触碰，含内核改动，未加载）**：55 dispatch 标签命名（逐组计数，零残留；未分发 ID 不命名）；PMR 三尺寸；stream/slot 三宏；15 旧门禁同步宏形式（两处误伤已纠正）。门禁 292+292，反向全过。见 `reports/r188-fn-table.md`。

- **r187 预设复核二轮 + bridge 审计（零硬件触碰，含内核改动，未加载）**：PCI 槽位宏统一（字面量仅剩定义处）、`0x88` 功能号命名进 wire.h（5 处比较）；其余 6 类预设故意保留（注释/断言在位）；19 WARN/锁序/分支全走查无动作项（释放语义冻结）。门禁 290+292，反向全过。见 `reports/r187-preset-audit-refactor.md`。

- **r186 scene 预设值抽取（零硬件触碰，含内核改动，未加载）**：bridge+6 live 的 scene VA 收敛到 `mt_addr_plan.h`（15 宏）；`MT_TQX_STATE_BYTES` 撞车（`mt_tqx_copy.h` 同名 `0xa8`）被 `W=1` 抓获后改名。门禁 287+292，反向全过。见 `reports/r186-addr-plan.md`。

- **r184 defaults 活体差分 Δ0（批准执行；无重载、无 GPU 工作）**：真实 blit 走 legacy（`0x89:0x0 → -25`，SIGABRT，exit 134；无 kick 无 prepare；DDK2 零调用），9 maps/11 mmaps/abort/close 后 probe 66→66、bridge 1→1，无 D 态。maps 无罪；+65 与 prepare/挂起强相关（kill-while-busy 精炼假设）。renderD128 另 2 持有者为本会话工具链（无 PMR）。见 `reports/r184-defaults-differential-d0.md` + trace。

- **r183 ref 离线审计（零硬件触碰，无代码改动）**：首要嫌疑已命名——`pvr_file_release` 双 early-return（unbind/destroy 失败即 return）可 abandon 整文件 PMR `dma_owner`（≈14/轮，与失败轮 +14 同形）；prepare 失败路与 DMA 注册/释放经走查配平，已排除。只读 `lsmod`：probe Used by=66（=1+65）、bridge 1。释放语义未动；活体差分待可重载窗口（需批准）。见 `reports/r183-ref-audit-offline.md`。

- **r182 bring-up 打通（批准执行；含内核改动，已恢复 freeze）**：`-22` 系 TQX 块在 process 前（`!p->store`），拆分后活体 `tqx-ctx: ready`；translator 持有 +18/rmmod -18 对称；失败轮 +65 未解释（功能无损）。代码 param 门禁入库（282+292，反向全过）。桥恢复默认 + L3 复绿。见 `reports/r182-tqx-bringup.md`。

- **r181 dry-run 活体验证（批准执行；含内核改动，已恢复 freeze）**：`translate_transfer` dry-run 上线（278+292，反向全过）；首轮选择 bug 修复；次轮程序 digest 与离线预言逐位一致；附带修 fill 未初始化（双门禁）。桥恢复默认 + L3 复绿（probe 1/bridge 0）。见 `reports/r181-dryrun-verified.md`。
- **r180 接线规约（只读 recon；零硬件触碰，会话未碰）**：scratch 中转架构（外页绑不进空间是决定性依据；表容量已够）；VA `0x49000000`；fill 五步 + copy 双面 + 门禁计划。见 `reports/r180-transfer-wiring.md`。
- **r179 fill 构造器（零硬件触碰，会话未碰）**：新增 `mt_transfer_fill.h`（池解析 + 矩形构造，错配大声拒绝）；C 门禁 16 项（274+288 全绿），反向验证通过；未接桥无发射。见 `reports/r179-fill-builder.md`。
- **r178 几何通道落定（零硬件触碰，无代码改动）**：5MB 池实转储——3841 零头 + `ff0000ff`×1310720（1280×1024，行连续）+ 254 零尾；64×64 复核 `3841+16384+254` 精确成立；颜色即像素字。见 `reports/r178-geometry-channel.md`。
- **r177 输出侧盘点（纯只读；零硬件触碰，会话未碰）**：DM2 空 marker、TQX fill 矩形、TQX copy 计划均有发射能力；Transfer 归 TQX（DM 只欠 TA/3D）。CCB 几乎全指针/标志，颜色几何不在其中——T3-transfer 首要缺口；`0xa3xxxx` 归属未定。门禁复核 274+272 全绿。见 `reports/r177-output-inventory.md`。
- **r176 T3 输入规约 v1（纯文档；零硬件触碰）**：冻结 envelope、header 字段表、body 拷贝、扩展区状态机、fill 实例账、未知清单、消费契约；DM 格式与完成语义明确在外。门禁复核 274+272 全绿。见 `reports/r176-t3-input-spec.md`。
- **r175 扩展区算术闭合（零硬件触碰，无代码改动）**：离线 GDB 读生成器 job（`c1064=0/c106c=0/c2c=1`，单 type=3 条目）；`0x1078+0x18+32+224+40=0x11B8` 内容终点；条目 `+8`=`uVar16` 写回；三段源与 39B 逐项对齐；载荷 B 为传输描述符（含 `0xa3xxxx` 小 VA）。T3 输入侧可解释。见 `reports/r175-extension-arithmetic.md`。
- **r174 真实 CCB 落定（批准执行；含内核改动，已恢复 freeze）**：`0x89:0xa` accept-and-log 上线（定界/鉴权/零嵌套读/零执行，5 门禁+反向；r150 断言改判）；真实 blit 39/39 非零字节与 fabricated 逐字节一致（仅 `+0x40` 轮变非单调，计数器命名收回）。桥恢复默认 + L3 复绿（probe 1/bridge 0）。见 `reports/r174-real-ccb-captured.md` + trace。

- **r173 门控活体兑现（批准执行；本轮零重载）**：mtime 对重载线证明双峰即参数窗口——`=2`→`0x89:0x8`→`0x89:0xa`（`-25`），默认→legacy `0x89:0x0`（`-25`）→有序自拆；桥 0x89 组仅 `{0x5,0x6,0x8,0x9}`，两路按设计拒收。会话健康（probe 1/bridge 默认在载）。见 `reports/r173-ddk-gate-live.md` + unwind 全 trace。
- **r172 DDK2 重载 + 真实 Submit3（批准执行；已恢复 freeze）**：`=2` 下 DDK2 全链 123 调用零非零；真实 blit 首个真实 `0x89:0xa`（`0/2/0`、`0x8000f44000`/`0x1200`，桥回 `-25`，无 GPU 工作）；真实 CCB 字节未捕获（Rss=0 之谜）。桥已恢复默认 + L3 复绿（probe 1/bridge 0）。见 `reports/r172-ddk2-reload-real-submit.md` + 双 trace。
- **r171 update 注入证伪（批准执行；freeze 继续）**：rung9（wire IN 摆 update）`RGXKickSync→1` 且无桥调用；语料：b26 是 UMD CMD 对象（count 在 `+0xD8`），84B 错位，且该函数无 update 组装——活体验证需 DDK2 重载（freeze 挡，待批）。会话健康（probe 1/bridge 0）。见 `reports/r171-update-inject-refuted.md` + trace。
- **r170 L4 全绿（GDB 监督；批准执行；freeze 继续）**：rung5 syncprim、rung6 kicksync 建销、rung7 compute 建销、rung8 `RGXKickSync→0`（inspect，无 GPU 工作）逐级全绿，四轮 trace 全 ret=0，probe ref 稳 1，dmesg 干净。rung5 standalone 崩溃仍在；监督非常规但调用/桥一致。见 `reports/r170-supervised-ladder.md` + rung8 trace。
- **r169 字节级无罪（批准执行；freeze 继续）**：10 组桥 OUT 全量对比，18 处差异全为调用方指针回显；另否 argv[0]/重试（rung5 standalone 0/20，GDB 5/5）；遮罩机制未解释。会话健康（probe 1/bridge 0）。见 `reports/r169-byte-exoneration.md` + 失败 trace。
- **r168 tid 猎杀（批准执行；freeze 继续）**：shim 25 处加 `tid`（门禁断言+反向验证）；失败/通过轮均为单 tid，交错假设证伪，桥前缀 65/65 一致；ASLR/SMP/perturb 全排除，GDB 4/4 过，发布者未命名。会话健康（probe 1/bridge 0）。见 `reports/r168-tid-hunt.md`。
- **r167 L4 部分通过（批准执行；freeze 继续）**：手跑阶梯（桥零重载）rung1–3 全绿；rung4 先 2 崩后 4 过；rung5 standalone 11/11 SIGSEGV、GDB 2/2 过——core 验尸为 `RGXCreateRenderContextCCB+1525` 取 `r12+8==NULL`，trace 全 ret=0、内核零错误、probe ref 稳 1，桥无罪。rung6–8 被阻。见 `reports/r167-l4-partial-segv.md` + trace + 验尸笔录。
- **旧会话记录（r211，批准执行；已于 r263 死锁重启后被 r264 取代）**：`mt_guest_probe` 曾绑定 `00:0e.0`（trial `20261007T040408Z-f3fb55af`，Guest/FW `2/2` pinned，ref 1），`mt_pvr_bridge` 默认参数在载（`card1`/`renderD128`，ref 0）；L3 全绿。见 `reports/r211-session-rebuild.md`。
- **旧会话记录（r166，批准执行；已于重启后被 r211 取代）**：`mt_guest_probe` 曾绑定 `00:0e.0`（trial `20261005T161706Z-cf0d876e`，Guest/FW `2/2` pinned，ref 1），`mt_pvr_bridge` 默认参数在载（`card1`/`renderD128`，ref 0）；L3 全绿（node 0 failing/0 mismatch，dma smoke PASS refs 平衡）；dmesg 无新增 WARN/BUG/Oops。见 `reports/r166-session-rebuild.md`。

- **r165（复核全绿；零硬件触碰，无代码改动）**：门禁重跑 269+272 全绿；blit 重放复现 r158/r160（同 VA/PMR，39B/27 runs）；SHA、无模块、r157–r164 文件逐项存在；订正 STATUS 现状两处过期（CCB 归属、update 语义）。见 `reports/r165-verification.md`。
- **r163（tq-perf 同 abort；零硬件触碰，无代码改动）**：`musa_tq_performance_test -n 1`（64×64）510 行后 SIGABRT，无 Submit3；GDB 栈与 r162 copy-blit 三重一致（同 aborter PC、同 `TQJobSubmit+738`、同位置）——同一阻塞点，非新 producer。按名断点因符号不可见 pending，止损。门禁复核 269+272 全绿。见 `reports/r163-tq-same-abort.md` + trace。
- **r162（换 producer 探针；零硬件触碰，无代码改动）**：`-n 2` fill 与基线机器比对仅 `+0x40` 计数器不同（38B 全等），fill CCB 与源面数无关；copy（去 `-f`）在 major 1/2 下同点 SIGABRT（`0x500d000` 映射后、`TQJobSubmit` 内 copy-setup 分发深处，无信息），当前不可达。扩展区算术缺新 producer，CCB 侧收敛。门禁复核 269+272 全绿。见 `reports/r162-producer-sweep.md` + 双 trace。
- **r161（离线 GDB 定写入者；零硬件触碰，无代码改动）**：对 PMR `0x500e` backing 窗口 `+0x40` 的硬件写观察点唯一命中 `SubmissionCmdGenerate` 的 `0x58B` 头拷贝（libc AVX 存），活体栈 `TQJobSubmit → SubmissionCmdGenerate → PVRSRVMemCopy` 确认 r156 路径；job `+0x40` 四轮 `0x288e→0x28ae→0x28bd→0x28d7` 单调递增，计数器形态、命名未定。门禁复核 269+272 全绿。见 `reports/r161-plus40-writer.md` + `r161-plus40-watch.txt`。
- **r160（fabricated CCB 窗口字段对照；零硬件触碰）**：shim `ccb_resolve` 加 `runs`（非零 runs 上限 32）；单轮 blit 重放 27 runs 恰好覆盖窗内 39B。`+0x10`=CCB+`0x58`、`+0x28`=`0x1078` 与语料 `SubmissionCmdGenerate` 的 `0x58`/`0x1020` 定长拷贝形状吻合；`+0x40` 的 2B 三轮各异（余 37B 一致），来源未定。`FUN_0015f890`（`TQSubmissionSubmit`）确认 check/update 编组链，本轮 `update_count=2` 与 r159 的 `flag&2` 相符。离线 269 Python（1 skip）+272 C 全绿。shim 回包仍 fabricated。见 `reports/r160-ccb-window-fields.md` 与 trace。
- **r159（update 语义离线确定；零硬件触碰）**：`SyncUtilGenerateUpdateData` 三要素
  与桥实现一致，无需改线布局；`flag&2` 来源待活体（禁重载）；清桥 DBG 残留，
  门禁全绿。无模块在载，`/dev/dri` 仅 `card0`。
- **r158（fabricated CCB VA→PMR 关联；零硬件触碰）**：shim 新增 VA 台账（`0x6:0x15` reservation 范围 + `0x6:0x13` pmr↔reservation），`0x89:0xa` 提交前按 r151 ABI 解出 `ccb_data@88`/`ccb_bytes@104` 并记 `ccb_resolve`（窗内非零/首非零/FNV-1a/32B 采样；不可达记 `resolved:0`）。重放离屏 blit（major 2 + shared backing，`timeout -s KILL 15` 终止）解码 `check=0/update=2/pmr_sync=0/ccb=0x8000f44000/0x1200`；`ccb_resolve` 给出 reservation `0x900d`（`0x8000f430fe`+`0xa00fff`）→ PMR `0x500e` → backing `0x500e000`+`0xf02`，窗内 39 非零、首非零 `+0x10`、FNV `0xb9e0f1a18201bf0f`；整块 10MB backing 非零同样 39、首非零 `0xf12`、采样相同，归属成立。新增 `test_pvr_shim_ccb_resolve` 合成门禁（含越界与反向）。离线 269 Python（1 skip）+272 C 全绿。shim 回包仍 fabricated，不证明执行。见 `reports/r158-ccb-backing-resolve.md` 与 trace。
- **r157（fabricated DDK2 producer；零硬件触碰）**：shim 新增 opt-in `UMD_DRM_MAJOR=2`（默认 major 1），离屏 `musa_blit_test -device 0 -f -o` 到达 `0x89:0xa`（108B/4B）；按 r151 ABI 描述解码的 CCB GPU VA=`0x8000f44000`、长度=`0x1200`。Submit 前 15 个 active shared-PMR snapshot 已记录；TDM/TQCB 区仍全零，heap/pool 非零数据未能映射归属 CCB。发现 snapshot registry 的 stale-unmap 风险，加入 unmap 清理和锁定，反向注入可使离线 replay SIGSEGV；恢复后 268 Python（1 skip）+272 C 全绿。Submit 回包为 fabricated，看到 ioctl 不代表接受或执行。下一步用 GPU VA 关联 PMR backing。见 `reports/r157-ddk2-tq-submit.md` 与 trace。
- **r156（fabricated producer 路径辨析；零硬件触碰）**：SHA 匹配的 5.2 UMD 语料表明 `TQJobSubmit`/`TQJobMultiSubmit` 调用 `SubmissionCmdGenerate()` 后经 `FUN_0015f890` 走 `0x89:0xa SubmitTransfer3`；r155 的 `musa_blit_test` 则走 `RGXTDMSubmit` / `0x89:0x4 SubmitTransfer2`，没有进入前者。GDB 抽查 legacy ioctl 嵌套栈块未识别到 CCB，不能外推其他 PMR。下一步寻找可离线复现的 DDK2 producer 输入，再追踪生成结果；静态语料属于路径假设，不是运行时 CCB 证据。见 `reports/r156-submit-path-split.md`。
- **r155（fabricated PMR shared backing；零硬件触碰）**：shim opt-in `UMD_SHARED_BACKING=1` 为每个 mmap handle 建独立 memfd，同 handle 映射共享、不同 PMR 隔离；LocalImportPMR 分配不同句柄，AcquireInfoPage 的映射才写 info-page 头。离线别名/隔离测试通过，注入 MAP_PRIVATE 可使其失败。UMD 到达 legacy `0x89:0x4`；Submit 前 TDM `0x1003000/0x1004000` 与三块 TQCB 映射全零，其他 heap/pool 区间有非零数据但用途尚未核实。门禁 265 Python（1 skip）+272 C 全绿。见 `reports/r155-shared-fabricated-pmr.md` 与 trace。
- **r154（fabricated 离屏 blit；零硬件触碰）**：`musa_blit_test -device 0 -f -o` 打印 TransferContext 创建、Submit 与 Wait OK，最终像素比对失败；578 行 trace、169 次 bridge ioctl 到达 legacy `0x89:0x4 SubmitTransfer2`（108B/8B），没有 `0x89:0xa`。UMD fd mmap 被 shim 替换成独立匿名映射，故该像素失败和 fake backing 抽样不能验证真实 CCB；后续应实现 handle/offset 关联 backing 再重放。见 `reports/r154-musa-blit-offscreen.md` 与 trace。
- **r153（fabricated Rogue2D replay；零硬件触碰）**：复用 `umd_connect_harness` 调 `R2DCreateContext(out)`，UMD 返回 0；95 条 bridge ioctl 中两次 `0x1:0xc` 均回 `num_cores=1`，随后走到 fabricated `0x89:0x5` / `0x89:0x0`，未出现 `0x89:0xa`。Surface 与 Layout 扩展调用返回 3，但没有新增 bridge ioctl，参数 ABI 尚未确认。见 `reports/r153-rogue2d-context.md` 与 trace。
- **r152（fabricated replay 适配；零硬件触碰）**：`probe/umd_bridge_shim.c` 对 `0x1:0xc` 回显 caps、fabricate `num_cores=1`，与 r150 bridge handler 对齐；新增回归测试，注入零核能失败。离线门禁 264 Python（1 skip）+272 C，shim `-Werror` 构建通过。完整 Rogue2D replay 尚未运行，SubmitTransfer3/绘制 CCB 未验证；bridge 仍不重载。见 `reports/r152-fabricated-multicore.md`。
- **r151（只读静态审计 + ABI 预备；零硬件触碰）**：`pvr_mmap()` 改逐页 vmalloc 转换并判空；
  修复 lazy VM 覆盖 DMA 已填 arena GPU 页表，以及 VM 未就绪时 close 的页表回收。5.2 UMD
  wrapper 静态恢复 `0x89:0xa` 108B/4B、check/update 各 32 项上限、PMR sync 17 项上限；
  packed wire 描述与关键 offset 断言已加，handler 尚未接入。离线 263 Python（1 skip）+272 C，
  `W=1` 全模块成功；逆向注入门禁均能抓回归。Oops 仍无 RIP/栈，根因未定，不重复加载 bridge。
  见 `reports/r151-bridge-null-leak-audit.md`。
- **r150（live 重验暂停）**：bridge 加载后 8 秒出现 DMA/CPU-only allocations；紧接 ChatGPT PID 2704 SIGSEGV 与 kernel NULL dereference（间隔 16 ms）。Oops 只有首行，无 RIP/调用栈；未运行 `musa_blit_test`。再启动后 S3000 `00:0e.0` 未绑定，Guest=`2`/FW=`1`，无 probe/bridge，仅 `card0`。不要直接重复加载 bridge；先查 Oops。详见 `reports/r150-tdm-core-count.md`。
- `0x1:0xc` 单核响应仍未 live 验证；TDM 双 PMR lifetime 与 TransferContext2 token 已由 r150 前次 bridge trace/离线门禁覆盖。r157 仅在 fabricated shim 中出现 `0x89:0xa`，未有真实 SubmitTransfer3 或真实绘制 CCB。

- **r149（批准执行，主线）**：`drm_major=2, translate_kick=1` 下真实 UMD
  DDK2 同链命中 `0x82:0x12`、`0x88:0x5`、`0x88:0x4`，均 ret=0；check-only
  kick 经 DM2 空 marker 完成（tag=1/fence=2）。桥 clean unload，probe ref 25→1；
  Guest/FW retained 2/2，当前 bridge 未加载，`/dev/dri` 仅 `card0`。真绘制 CCB、
  TA/CDM 专属口和 update 数组语义仍未验证。见 `reports/r149-ddk2-translated-check.md`。
- **r148（批准执行，主线）**：check-only kick 经真实空 marker 回 0（tag=1/fence=1）；
  正常 bridge teardown 首次活体验证通过：`unloaded cleanly`，probe ref 25→1，
  无新增 WARNING。初次错误期望值产生 -ETIMEDOUT，无 marker 提交；修正到 PMR 实测值后成功。
  见 `reports/r148-translator-teardown.md`。


- **r147（批准执行，主线）**：check-only 首帧打通。UMD check-kick 经真实
  DM2 空 marker 回 0（×3：tag 1/2/3，fence 9/10/11；REF 28 平；零 WARNING）。
  修 5 处：模板循环丢增量（soft lockup，重启恢复）、跨模块 ops 复本、
  持锁等 fence 自饿、RT+CSW 补丁、fence 引用漏 put。
  桥以 `translate_kick=Y` 在载（ref 28/0），L3 全绿。**继续 freeze。**
  下一轮：Translator 收尾（update 侧诚实拒绝已在；真绘制 CCB 仍缺）或 DDK2 纵深。
- **r146（批准执行，主线）**：DDK2 kick 语义。`=2` 下零 count 同步 kick
  仍走 `0x88:0x4`（84B）原样受理，全链全绿；TA/CDM 专属口（`0x82:0xC`/
  `0x81:0x5`）属 S4 不碰。桥恢复默认（ref 1/0），L3 全绿，零 WARNING。
  **继续 freeze。** 下一轮：Translator T3（r113 首帧执行）。
- **r145（批准执行，主线）**：`0x2:0x8` 落地后 DDK2 全链首绿（六符号全 0，
  122 行轨迹零非零；`b5*` 复验成立）。新会话：`mt_guest_probe` 绑定 `00:0e.0`
  （Guest/FW `2/2` retained pinned，trial `20261004T084935Z-5d5c38fb`，
  引用数 **1**），`mt_pvr_bridge` 在载（默认参数，引用数 0），
  `/dev/dri` 有 `card1`/`renderD128`（桥）。L3 全绿；L4 未跑（UMD 阶梯另行）。
  dmesg 零 WARNING，无 D 态。**不要卸载任何已加载模块、解绑设备或提交额外工作。**
- **r144（主线诊断）**：分配器崩溃系 harness 传参（`b5*` 后全绿，见报告），
  非桥缺口；`0x2:0x8`=SyncFreeEvent 为下一实现目标。
  重启后无任何 `mt_*` 模块加载，`/dev/dri` 仅 `card0`，`/tmp` 已清空，
  r125–r143 会话不存在。**重建（r138 流程）待批准后执行。**
- **r143（批准执行，主线）**：DDK2 CCB 建销落地。`=2` 同链 render/syncprim/0x88:0x5
  全回 0；UMD 随后在 `SubmissionBufAlloctorCreate` 空解引用（gdb 活体三帧栈，
  内核零异常——r134 预言的门控下游）。桥恢复默认（ref 8/0），L3 全绿。
  **继续 freeze。** 下一轮：分配器输入只读追踪。
- **r142（批准执行，主线）**：DDK2 render 建销落地。`=2` 同链 render 首返 0、
  syncprim 0；`0x2:0x2` 归属为 SyncPrimSet（stub-ok）；续堵于 `0x88:0x5`
  （CCB2，仍 `-ENOTTY`，UMD 随即用户态段错误，内核零异常）。
  OUT 以活体 12 为准（订正 r141 的 4）。桥恢复默认（ref 8/0），L3 全绿。
  **继续 freeze。** 下一轮：`0x88:0x5` 及其 destroy 对端。
- **r141（批准执行，主线）**：`drm_major=2` 首触 DDK2：同链 render → 37
  （=0 对照全绿，91 调用），差值 = 3 新桥命令全 `-ENOTTY`
  （`0x82:0x12`=CreateRenderContext2、`0x88:0x5`=CreateKickSyncContext2 已按名归属，
  `0x2:0x2` 待定）+ AlignmentCheck 消失（r134 预测兑现）；37 系 stub 失败常量；
  UMD 内 DDK2 桩共 18 个。桥已恢复默认（ref 0），probe ref 8，零 WARNING。
  **继续 freeze。** 下一轮：按依赖实现 `0x82:0x12/0x13` → `0x88:0x5` → CCB 期再定。
- **r140（批准执行 ②-1）**：Δ1 猎杀终结。`lease_free` 补 `drm_gem_object_release`
  + `drm_dev_put`（与 `mt_live_drm.c`/`mt_gem.h` 对齐；仅 live_3d_drm.ko 重编，
  probe/bridge 未动，会话保留）。单 fill 与全 smoke 卸后 ref 均为 **8**（Δ0），
  dmesg 零 WARNING，L3 未重跑（bridge 未动）。probe 引用数 **8**（历史冻结，
  不再增长）。**继续 freeze。**
- **r139（批准执行 ②）**：修复已部署并验证。`mt_guest_probe` 绑定 `00:0e.0`
  （Guest/FW `2/2` retained pinned，trial `20261004T070741Z-1966ee3f`，引用数 **2**），
  `mt_pvr_bridge` 已加载（修后构建，引用数 0），`/dev/dri` 有 `card2`/`renderD129`。
  空载周期 Δ0（MID=36 卸后回 1），带帧周期 Δ1（3/3 零 fault，卸后 2）。
  L3 全绿；L4 未跑。dmesg 零 WARNING（destroy WARN 消失）。
  **不要卸载任何已加载模块、解绑设备或提交额外工作**（Δ1 猎杀除外，需批准）。
- **r138（批准执行 ①）**：会话重建完成。`mt_guest_probe` 绑定 `00:0e.0`
  （Guest/FW `2/2` retained pinned，trial `20261004T065633Z-546d5bc5`，引用数 **86**），
  `mt_pvr_bridge` 已加载（在盘新鲜构建，含 `drm_major`，引用数 0），
  `/dev/dri` 有 `card2`/`renderD129`（桥；minor 顺延）。
  Δ1 二分：首周期+34，空载+25，带帧+26（3/3 completed 零 fault）。
  L3 全绿（node 0 failing + dma smoke PASS，refs 86→87→86）；L4 未跑。
  dmesg 6 WARNING（全 destroy -EBUSY，无新模式），无 D 态。
  **不要卸载任何已加载模块、解绑设备或提交额外工作**（② 的修复验证除外，已批准）。
- **r137（只读诊断，零硬件触碰）**：+26/周期根因静态闭环——`fini` 见 sealed 即 `-EBUSY`
  （`mt_gpu_vm.h:309`），两 space 皆 seal 且全树无 unseal，destroy 的 2 条 WARN 即证据；
  每周期 25 backing 不释放（3+8+11+1+2 tables），首周期 +34 另含一次性 boot borrow 9；
  稳态 Δ1 未归属，需活体二分。修语义单独立项。证据见 `reports/r137-sealed-vm-ref-leak.md`。
- **r136（只读排查，零硬件触碰）**：uptime 仅 23 分钟，无任何 `mt_*` 模块加载，
  `/dev/dri` 仅 `card0`，`/tmp` 1%（`opencode` 空、UMD 解包目录已清空），
  `dmesg` 0 WARN、无 D 态——当前无泄漏在发生，r125–r134 retained 会话已不存在。
  内核零文件写调用；`/tmp` 写者全是用户态（shim trace 有 256MB 顶）。
  “几小时卡死”候选：①live 每周期 +26 probe 引用（r44/r127–r130）；②r67 device-mutex；
  ③Chrome 持 renderD128。重建待批准。证据见 `reports/r136-leak-tmp-audit.md`。
- 以下为重启前记录（已过期，仅保留原文）：**r133**：非零 CCB create 在 `ddk_feature_set=2` 与默认下桥调用 91=91 一致；
  模块停在默认参数新桥，引用 0/113，无新 WARN，继续 freeze。

- **r132 桥已重载（默认参数）**：`mt_pvr_bridge` 先以 `ddk_feature_set=2`
  跑 rung5，再换回默认重载；两次桥调用逐项一致（89=89），DDK2 未确认可达。
  当前加载的是含 `ddk_feature_set` 参数的新桥（默认 0），引用 0/113，
  无新 WARN。`mt_guest_probe` 继续 freeze。

- **r130 契约裁决 + 首个绘制像素**：out_syncobj=0 分歧裁决为测试过期
  （`if (r->out_syncobj)` 自 r40，强制要求从未存在；兄弟工具无此期望）。
  修正后 smoke 全绿：13 非法拒 + 0-syncobj fill（seq=2）+
  16×16 `0xff123456` 三重像素验证 + copy 闭环（3/3/0）。
  模块已卸（WARN 累计 8 条，全同签名）。probe 引用 87 → **113**
  （又是 +26）。桥探针复核全绿，继续 freeze。

- **r129 fill smoke 红（契约分歧，非回归）**：`mt-fill-check smoke`
  倒在 bad[3]（0-syncobj：测试要 EINVAL，驱动接受执行，与 uapi
  Optional 注释一致）；旁证 +1 fill（1/1/0，零 fault）。模块已卸
  （同签名 WARN 累计 6 条）。probe 引用 61 → **87**（又是 +26；
  r127 的 +34 仍是孤例）。桥探针复核全绿，继续 freeze。

- **r128 RT 绑定帧成功**：同一模板带 RT 绑定（`0x45a0` 非零路径）
  `frame_tag=2` → `seq=2`，`completed` 再 +1，零 fault；
  64KiB 读回全 `0x5a`（空 marker 无绘制，符合设计）；
  GEM 创建/关闭完整。模块已卸（同签名 WARN 累计 4 条，无新模式）。
  probe 引用 35 → **61**（本周期 +26，同 r44；r127 周期的 +34 差异未解释）。
  桥探针复核全绿，继续 freeze。
- **r127 活体首帧成功**：`mt_live_3d_drm` 已加载执行并卸载
  （做完即卸；留 2 条 r44 同类 sealed-VM WARN，taint 现 `12800`=OE+W）。
  DM2 空包（frame_tag=1，无 RT）`seq=1`，`completed 0→1`，零 fault；
  `0x4668` 直通值被固件接受。probe 引用当时 stays **35**
  （已知泄漏类）；`mt_guest_probe` + `mt_pvr_bridge` 保持加载，继续 freeze。

- **新 retained 会话运行中（r125，用户已批准重建）**：`mt_guest_probe`
  已绑定 `00:0e.0`（Guest/FW `2/2` retained pinned，trial
  `20261003T162138Z-604b34a6`，引用数 1），`mt_pvr_bridge`
  已加载（build-id `2c6bede3…`，111 构建，引用数 0），
  `/dev/dri` 有 `card1`/`renderD128`（桥）。
  UMD 在 `/tmp/mtt-linux-umd-5.2.0/…`（树内留档恢复，sha `b3058c02…`），
  L3 三探针 + L4 八级阶梯在本桥上全绿（见 `mt-vgpu-guest/reports/r125`）。
  dmesg 零 WARNING/BUG/Oops，无 D 态任务。
  **不要卸载任何已加载模块、解绑设备或提交额外工作。**
- 以下为 r123–r124 记录（重启后会话已失，已被 r125 重建取代，仅保留原文）：

- **机器发生外部重启（r123），上一节所述 retained 会话已不存在**：
  当前无任何 `mt_*` 模块加载，`00:0e.0` 无驱动绑定，
  `/dev/dri` 仅 `card0`，`/tmp` 内 UMD 与 trace 已清空。
  仓库完好（`HEAD b4e0b5a`，57 提交未 push），L1 全绿，
  树内 UMD 留档可用。重建（r68/r69 流程）待明确批准。
- **内核漂移（r124，只读+离线实测）**：运行内核已是
  `6.12.111+deb13-amd64`（107 headers 并存）；在盘
  `kernel/recovery/mt_pvr_bridge.ko` 的 `.modinfo` 实测
  `vermagic=6.12.111`，与运行内核一致，重建不需重编，只等加载批准。
  L1 本轮重跑仍全绿（226+1 skip，268 C）。
- 以下为重启前记录（已过期，仅保留原文）：新 retained 会话运行中：`mt_guest_probe` 已绑定 `00:0e.0`（Guest/FW
  `2/2` pinned，`pending=0/completed=23`，引用数 38），`mt_pvr_bridge` 已加载
  （build-id `894faf50…`，arena+cover+kick-inspect，引用数 1——
  Chrome 被动持有 renderD128，不影响 ioctl 实验；见 r85），
  `mt_live_3d_drm` 留存（sealed 3D VM 不可卸载），`/dev/dri` 有
  `card1`/`renderD128`（桥）与 `card2`/`renderD129`（3D）。
  UMD 在 `/tmp/mtt-linux-umd-5.2.0/…`，L3/L4 八级阶梯在本 bridge
  上全绿（见 `mt-vgpu-guest/reports/r52`）。**首次 RGX 真实执行已完成
  （单帧 DM2，`completed=1 result=0`，sealed 3D VM 留存；见 r66），
  像素级验证随后通过（render-target 64 KiB 读回；见 r70），
  20 帧批量零 fault（见 r71；对象存储已满，需空存储的实验会被拒绝）。
  **在盘桥已不是在载桥**：`kernel/recovery/mt_pvr_bridge.ko`
  含 r88 的 `0x89` TDM 实现（只待加载窗口，不会自动生效）。
  不要卸载任何已加载模块、解绑设备或提交额外工作。**
- 以下为上一轮记录（已过期，仅保留原文）：本轮真机验证完成后，机器发生
  了一次外部重启。当前 `mt_pvr_bridge` **未加载**，`/dev/dri` 只有 `card0`，
  `/tmp` 中的 UMD 与 trace 已清空；树内留档 UMD 仍在，
  sha256 `b3058c02…` 可恢复。已验证的结果见 §1–§6，
  重新跑 L3/L4 需要先显式批准加载新模块。
- `mtgpu` 独占 `00:0e.0`；我们用独立 `pvr` 节点 `renderD128`
- **不需要重启**（上一轮的自死锁已随重启清除）
- 厂商 UMD 位置：`/tmp/mtt-linux-umd-5.2.0/root/usr/lib/x86_64-linux-gnu/libsrv_um_MUSA.so.1.0.0`
  （树内留档：`build/legacy-umd-pvr-connect-candidate/rootfs/usr/lib/x86_64-linux-gnu/`）
- **未决问题**：刷机版本仍未定。
  `build/official-vgpu-r22b-mmu-mapping-audit-20260929/mtgpu.ko`（最新，09-30 00:09）
  vs 已安装的 09-28 21:34 那个（sha256 `84817b6d…`，不匹配任何 93 个产物）。
  **`/lib/modules` 至今未动过。**
- **S4（真实硬件提交）仍需单独批准。**

- **r375** (2026-10-08): R5 基础设施实现（per-file TA VM 上下文 + 映射流程），门禁全绿；活体因 `bind_many` oops 中断，bind 已禁用，系统待重启。V1/V2 未完成。
- **r376** (2026-10-08): R5 VM 初始化重新设计——bridge 侧 `mt_bridge_ta_vm_create()` 用合成 BO + 正式 `mt_gpu_vm_init()`（遵循 3D 模式），删除 r375 手动拼装（oops 根因）；probe 因 trial pinned 未重载（无需 probe API）；门禁 428+299 全绿，W=1 零警告；bridge 已重载，V1/V2 活体待 harness 修复。

### r377 (2026-10-08): Harness INIT 修复，V1/V2 活体验证通过
- **Harness 修复**：r376 的 INIT 传参错误（init_module 非 1/2 → EINVAL）；按 r373 既证格式（u32 module=2）重写，一次通过。
- **V1**：`mt_bridge_ta_vm_create()` 成功（proper init，无 oops）。
- **V2**：`mt_gpu_vm_bind_many()` 空绑定返回 `-EINVAL`（预期），无 oops。r375 oops 根因消除。
- **回归**：两次 TA kick 的 OUT.update_fence 与 dmesg wire 精确匹配（1/1、2/2）。
- 门禁 428+299 全绿；本地提交待执行。

- **r378** (2026-10-08): 真实页表绑定验证通过——1 真实页绑定到 VA 0x70000000，`mt_gpu_vm_bind_many()` 返回 0，无 oops；r375 oops 根因彻底消除；marker 回归正常；firmware VA 翻译待验证。
- **r379** (2026-10-08): 0x82:0x14 (MUSAKICKGFX5) 调研——R3 缺口现状：桥侧为 r215 accept-and-log observer（108B IN 解码后返回 0，不执行）；wire 结构已入库（KMD 5.2.0 头）；与 0x82:0xC 差异：单一 submission、render_context 显式、OUT 无回填；设计 DM2 + 第 6 op；V1–V4 待活体验证。门禁全绿。
- **r380** (2026-10-08): DM2/0x66 单发 marker 被 firmware 忽略（2s 无事件，timeout）；副作用致 trial 会话被 firmware 清除（0x890 2→0），需冷重启恢复；0x66 非 3D opcode，0x64 对照未测；无 oops，模块未重载，探针已卸载。
- r381 (2026-10-08): 3D opcode 研究（离线，零硬件触碰）：3D (DM2) 的 firmware opcode 为 0x68 (RGXCompute, type 5)；0x66 在 DM2 上仅对真实命令包有效（mt_live_3d.c 实证，r37–r41），空 marker 被忽略（r380）；完成码预测为标准 0；Windows KMD (mtkm64.sys) 确认 RGXCompute；建议 0x82:0x14 实现用 0x68，须构造完整命令包。门禁 428+299 全绿（待跑），本地提交未 push。
- r382 (2026-10-08): submit_3d_work 落地（第 6 op，0x68，门控关闭，离线）：mt_marker_ops 第 6 op（仿 r366），DM2/0x68 (RGXCompute)，标准完成码 0；MT_3D_SUBMIT_GATE=0 默认关闭，op 返 -EOPNOTSUPP；0x82:0x14 dispatch 保持 r215 observer 未切换；submission_va R5 映射仅留 vm_map_hook 占位；零硬件触碰；门禁 430+299 全绿，make kernel W=1 零新增警告，反向验证通过；本地提交未 push。
- r383 (2026-10-08): probe-side r376 dead code cleanup (offline, zero HW touch): removed kernel/mt_probe_ta_vm.h (29 lines) + 122 lines in mt_guest_probe.c; bridge comment updated; grep zero refs; kernel W=1 zero warnings; gate 430+299 green.
- r384 (2026-10-08): R6 DDK2 context statefulness gap analysis (offline, zero HW touch): 0x82:0x12/0x88:0x5/0x89:0x8 all handle tokens, no firmware state; real context needs 11 BOs+CSW+exec ctx (mt_live_3d.c proven); R5 VM is per-file not per-context (design tension A/B); markers unblocked, real UMD rendering blocked; gap list R6-1..R6-6.
- r385 (2026-10-08): R7 Sync prim import gap analysis (offline, zero HW touch): ZeusSyncPrimImportFD=SYNC:0xC (BridgeSyncPrimImportFD, IN 24B) NOT implemented—no MT_PVR_FN_* 0xC in mt_pvr_wire.h, dispatch default returns -ENOTTY; SYNC 0x0/0xA real, 0x1/0x2/0x7/0x8 stubs; UMD TA path includes ImportFD→0x82:0xC (r361), likely blocks real UMD TA; independent of R6 (context vs sync prim objects); gap list R7-1..R7-4.
- r386 (2026-10-08): SyncPrimImportFD (0x2:0xC) implemented (offline, zero HW touch): MT_PVR_FN_SYNCPRIMIMPORTFD 0xcU + 24B IN/12B OUT structs from KMD 5.2.0 header in mt_pvr_wire.h; pvr_cmd_syncprim_importfd() validates hSyncBlock via translator_resolve + FD via fdget/fd_empty, returns current u32 value; FD payload interpretation TO-VALIDATE (needs ExportFD producer + live UMD); gate 438+299 green, kernel W=1 zero warnings, reverse validation passed.
- r387 (2026-10-08): R6 Route A per-context design (offline, zero HW touch): user decided Route A (per-context state, not per-file); struct mt_pvr_render_context carries 11 BOs (86KB) + CSW + exec process/ctx + per-context VM (VA partitioned); create/destroy/kick lifecycle designed; R5 gradual migration (Phase 1 coexist with fallback, Phase 2 switch after validation); R6-1~R6-6 broken down (R6-5/R6-6 need research first); 6 open validation points recorded.
- r388 (2026-10-08): R6-1 per-context struct defined (offline, zero HW touch): kernel/mt_render_context.h new (struct mt_pvr_render_context 1456B: 11 BOs + vas/bos_ready + exec process/ctx + 248B CSW + per-context VM ptr + vm_base_va); mt_pvr_object += render_ctx pointer (kzalloc NULL default, pvr_object_new unchanged); 3 layout gate tests (sizeof/offsets/kzalloc); gate 441+299 green, kernel W=1 zero warnings, reverse validation passed.

### r389 (2026-10-08): R6-2 Create realized, live V1 verified
- `mt_render_context_create()` in `kernel/recovery/mt_pvr_bridge.c`: 11 BOs + init data + per-context VM bind + CSW + exec (node_type=5), rollback on failure.
- `mt_render_context_vm_create()`: d->buffers-backed 64KB page tables (fixes store/ops mismatch vs r376 synthetic VM).
- `pvr_cmd_render2_create` (0x82:0x12) calls it; no longer empty token.
- Live V1: single create → 11/11 BOs bound, CSW built, exec created, READY, zero oops. Bridge reloaded once.
- Gate: 450+299 green, kernel W=1 zero warnings, reverse validation passed.
- Honest: V1 only; R6-3 (destroy), R6-4 (kick parse), V3/V4 future.

### r393 (2026-10-08): R6-6 TDM research verdict — no server-side TDM context needed (offline)
- `0x89:0x8` (RGXTDMCreateTransferContext2) empty token is sufficient; R6-6 closed as wont-do by design. TDM = Transfer Data Manager (2D/blit engine, KMD `common_musaxfer_bridge.h`).
- r150 live proof: real UMD TDM lifecycle (0x89:0x8 create -> 0x89:0xa submit -> 0x89:0x9 destroy) all ret=0, UMD proceeds normally; r174 live: 0x89:0xa accept-and-log captured real CCB bytes from UMD.
- Submit only existence-checks the handle (`have_ctx`); no consumer of TDM context server-side state. Only real TDM resource is shared-memory PMRs (0x89:0x5, CLI+USC separate, already implemented, r150).
- Independent of R6 (render ctx); does not block real UMD. Reopen if real TDM execution is ever implemented. Gate: check-offline green (research only, no code).
### r392 (2026-10-08): R6-5 CCB research verdict — no server-side CCB needed (offline)
- `0x88:0x5` (BridgeRGXCreateKickSyncContext2) empty token is sufficient; R6-5 closed as wont-do by design. CCB = command ring, two-sided: UMD-side SubmissionBufAllocator (userspace, render ctx +0x200, r199) + server-side device-memory ring (Windows KMD internal, r56 rung6: no UMD-visible PMR/heap).
- Our DDK2 path builds firmware packets directly in-kernel (DM3/0x66 r366, DM2/0x68 r382) and submits via DM — bypasses the CCB-ring model entirely; 0x82:0xC / 0x82:0x14 IN have no kicksync field; 0x88:0x2/3/4 only existence-check the handle.
- r144 live proof: after harness fix (&b5 -> b5*), full CCB lifecycle (0x88:0x5 create -> 0x88:0x6 destroy) all ret=0, zero crashes — r143 crash was a harness bug, not a bridge gap.
- Independent of R6 (render ctx); does not block real UMD. Gate: check-offline green (research only, no code).
### r391 (2026-10-08): R6-4 Kick-side render_ctx parsing, live V3 verified (no oops)
- `pvr_cmd_musakickgfx2` (0x82:0xC) resolves `h_render_context` via `pvr_object_find(..., MT_PVR_KIND_CONTEXT)`; uses per-context VM when `resources_ready`, else per-file fallback (Phase 1 marker protection). V2 bind validation on selected VM.
- TA marker keeps TA-dm context (render_ctx exec_ctx is node_type 5/DM3D; real exec submission = R6-5). 0x82:0x14 unchanged (observer).
- Live: bridge reloaded once. Marker regression: no-ctx (fence=1, per-file) / ctx1 (fence=2) / ctx2 (fence=3) all error=0, OUT.update_fence matches wire. V3: two contexts, separate VMs (VM-level isolation; same vm_base_va by design), correct per-handle routing, no leak (file-close 25→13). dmesg zero WARN/BUG/Oops.
- Gate: 463+299 green (+5 new `test_kick_render_ctx.py`), kernel W=1 zero warnings, reverse validation passed.
- Honest: exec_ctx not used for TA (dm mismatch, by design); VA ranges overlap (page-table isolation); firmware VA translation TO-VALIDATE.
### r390 (2026-10-08): R6-3 Destroy realized, live V2 verified (no leaks)
- `mt_render_context_destroy()` in `kernel/recovery/mt_pvr_bridge.c`: reverse-order teardown (exec ctx → exec process → 11 BOs put → VM destroy); safe on partial init via exec_ready/bos_ready/vm-NULL guards; WARN_ON on failures.
- Hooked into `pvr_cmd_handle_release` (0x82:0x13 + DDK2 destroy, kind==MT_PVR_KIND_CONTEXT) and `pvr_file_release` (V4 file-close cleanup).
- Create's `out_rollback` refactored to reuse destroy (single path).
- Live V2: V2a explicit destroy 25→13 refs (delta -12, all released); V2b file-close 25→13 refs. dmesg zero WARN/BUG/Oops. Bridge reloaded once.
- Gate: 458+299 green (+8 new tests), kernel W=1 zero warnings, reverse validation passed.
- Honest: probe ref baseline 13 (r389 old leak, clears on cold reboot); partial-init destroy not fault-injected live; V3/R6-4 future.
- r394（2026-10-08）：live 前安全测试落地（离线）：三类事故复盘→三类门禁——T1 `tests/test_vm_init_integrity.py`（VM 内部字段赋值禁区：ranges/page_lists/bindings 等禁出 `mt_gpu_vm.h`）；T2 `tests/test_opcode_whitelist.py`（`(dm,opcode)` PROVEN 白名单：trial/DM0、TQX/DM1、3D/DM2、TA/DM3，`(2,0x66)` 永禁，TA 钉 DM3、3D 钉 DM2）；T3 `tests/test_pre_live_safety.py`（强制卸载仓库黑名单）+ `mt-vgpu-guest/scripts/safe_rmmod.sh`（refcount 非 0 拒绝）；pre-live 检查清单 5 条（`(dm,opcode)` 查表 / VM 走 init / 不用 -f / trial 前置 / 单模块）；反向验证 RV1–RV3 全过；门禁 472+299 全绿；本地提交未 push。
- r395（2026-10-08）：安全网首个实战检验（活体）：pre-live 门禁 T1/T2/T3 全绿（check-offline 472+299，safe_rmmod.sh 0755 待命）；两次真实 0x82:0xC TA-only kick（r377 harness 模式），OUT.update_fence=4/5 与 dmesg wire=4/5 精确匹配，零 completion timeout（0x100 完成到达），dmesg 零 WARN/BUG/Oops，refs 不变（bridge 0/probe 13）；零模块操作、零 rmmod -f；门禁全绿；本地提交未 push。
- r396（2026-10-08）：exec_ctx 接入 kick 路径设计（离线）：render_ctx 双执行上下文（exec_ctx_3d node_type=5→DM2 既有改名 + 新增 exec_ctx_ta node_type=2→DM3），共享同一 process；TA kick 传真实 exec_ctx_ta 替代 throwaway ctx（门禁 route.dm==3 通过，T2 白名单一致）；三阶段落地（加字段→替换→真实payload），marker 行为中性；TA 仍走 submit_ta_work（0x100/sync 回写/D8 拒收），完成双路径已处理 m->context；零代码改动、零硬件触碰；门禁全绿；本地提交未 push。
- r399（2026-10-09）：R5 Phase 2 活体验证（活体）：pre-live 门禁 T1/T2/T3 全绿后，bridge 经 safe_rmmod.sh 重载到 r398 构建（probe 不动）；V1 无 ctx 的 0x82:0xC kick 返回 -EINVAL（errno 22，dmesg 有 r398 Phase 2 标记）；V2 先 0x82:0x12 create（handle=0x1000）再带 ctx kick 成功，OUT.update_fence=10 对应 dmesg wire=10 精确匹配；V3 destroy 后 probe ref 13->25->13 delta 归零无泄漏；dmesg 零 WARN/BUG/Oops；门禁 474+299 全绿，kernel W=1 零警告；本地提交未 push。
- r400（2026-10-09）：代码目录重构（离线）：Phase A 清理 kernel/ 三目录构建产物；Phase B tests 重组为 c/pvr/ta/guest/render/misc 子包（130 文件 git mv）；Phase C 暂缓；Phase D 更新 README；门禁 474+299 全绿，kernel 零警告；本地提交未 push。
- r401（2026-10-09）：代码重构机会分析）：7 个机会按优先级排序——P0 测试 helper 提取（47 文件重复 ROOT，2 种不一致写法，无共享模块）；P1 TA/3D submit 统一（~70% 重复，提公共 ops）；P2 长函数拆分（prepare_locked 294 行、dispatch 168 行）；P3 魔法超时值命名常量；死代码零；ENOTTY/EOPNOTSUPP 有意区分；kernel 头文件重组暂缓；门禁 474+299 全绿；本地提交未 push。
- r402（2026-10-09）：测试 helper 提取（离线）：新建 mt-vgpu-guest/tests/helpers.py（get_repo_root/get_kernel_dir/get_scripts_dir/get_reports_dir/get_tests_dir/get_build_dir/get_kernel_header）；75 文件的 repo-root 路径表达式统一收敛（2 种写法→1 种）；import 保持 discover/直接执行双模式；修复 3 文件 import sys 后置 NameError；反向验证通过；门禁 474+299 全绿；本地提交未 push。
- r403（2026-10-09）：TA/3D submit 统一（离线）：r401 P1 落地——提取 mt_marker_submit_engine_work() 公共框架 + struct mt_marker_submit_ops（dm/validate_params/build_command/store_params），两函数变 thin wrapper；~65 行重复框架逻辑两份合一（旧 188 行→新 208 行，第三引擎边际成本 ~100→~35 行）；3D 门控 #if 原样保留（关门时零编译零警告）；T2 白名单三测试适配 ops 表结构，反向验证 RV1（dm 篡改）/RV2（3D hook 引 TA opcode）均 FAIL 符合预期；门禁 474+299 全绿，kernel W=1 零警告；零硬件触碰；本地提交未 push。
- r405（2026-10-09）：真机适配缺口分析（离线）：定义真实可用四级——T0 API 不报错（r144/r172 123 调用零非零，已达）→ T1 GPU 真实执行（核心缺口：仅 marker 空包，零真实 TA/3D 命令）→ T2 像素可验证（DDK2 无回读基础设施）→ T3 UMD 集成（r358 建连/r172 全链通，TA/3D 从未真实执行）；T4 桌面栈 out-of-scope；缺口 G1 真实 payload（P0，3–5 轮）> G3 门控（P1，3D 零活体）> G4 回读（P2）> G2 UMD 集成 > G6 长尾（kick_pr=1/sync-prim/16MB stride）> G5 压力；路线图 r406 3D marker 活体 → r407 TA 捕获 → r408 解析 → r409 真实 TA 活体（高风险）→ r410 回读 → r411 3D 门控 → r412 UMD triangle；纯分析零硬件；本地提交未 push。
- r406（2026-10-09）：3D 包活体提交——固件无响应（活体）：pre-live T1/T2/T3 全过，(2,0x68) 白名单确认；桥侧测试钩子 mt_bridge_3d_test_submit() 直接调用 mt_marker_submit_engine_work()（绕过 MT_3D_SUBMIT_GATE，门控保持 0）；3D 包（opcode 0x68 @+0x0c，VA @+0x28，size @+0x30，wire_id @+0x48，r382 布局）提交成功，fence 已分配；固件 5s 内无完成事件（dma_fence_wait_timeout 返回 0，-ETIMEDOUT），签名 submitted-but-ignored；结论：3D 基础设施工作正常，固件需真实负载非 marker，与 r380/r405 G1 一致；测试桥仍在载（ref=1，pending 3D fence 持有，无法卸载），源码已恢复 r404，系统稳定无 oops，待用户冷重启清除；门禁 474+299 全绿；本地提交未 push。
- r407（2026-10-09）：TA 命令缓冲捕获机制验证（活体观察）：冷重启后 pre-live T1/T2/T3 全过；桥侧临时 hook（copy_from_user + print_hex_dump）在 0x82:0xC 入口成功捕获 TA 命令缓冲前 64 字节（ta_size=360 与 r363 一致）；捕获为 fabricated 测试模式，机制已验证；TA 包布局：DM 包 80B（opcode 0x66 @+0x0c，wire_id @+0x48，pid @+0x4c）+ TA 缓冲 360B（指令流待解析）；hook 已移除，桥重载干净构建；dmesg 无 WARN/Oops；门禁全绿；本地提交未 push。
- r408（2026-10-09）：RGXSubmitTA 不构造 TA 缓冲（离线反汇编）：MUSA DDK 5.2.0 UMD 调用链 RGXKickTA(0x17afd0)→RGXPrepareTA(FUN_00178800,218行)→RGXSubmitTA(FUN_001796b0,709行)→BridgeRGXKickTA3D3(FUN_00137180)→SubmitTADataEnQueue；SubmitTA 内 0x168=360 为立即数尺寸参数、VA 取自 psKickTA[0]，全函数无缓冲写入/模板填充；PrepareTA 只做指针搬运（psKickTA[0..2]=render_ctx+0xb6/b8/ba）+0x1c8-0x1d4 状态回填；psKickTA 无 UMD 侧构造函数（r193 corroborate）；360B 内容由客户端 3D 状态机生成；r409 三条路：真实应用 trace（推荐）/KMD 文档/盲探（不推荐）；门禁 474+299 全绿；本地提交未 push。
- r409（2026-10-09）：真实 TA 路径缺口确认（离线+实测）：标准程序 egltri_x11/glxinfo 实测走 Mesa llvmpipe（0 次 musakickgfx2 dispatch），不走 0x82 DDK2 桥，不可达；离线反汇编 FUN_00169240 解析 TA 缓冲结构——40B（5 qwords）/72B（9 qwords）条目序列，render_ctx+0xb6 指针推进填充，TA state buffer，调用链 FUN_00169240→RGXKickTA→PrepareTA→SubmitTA（透传 VA）；当前 mt_ta_submit_build 仅发 80B marker（ta_params 存不发）；r410 需扩展包构建器（含 360B VA）+ DMA VA 映射 + 条目语义；pre-live T1/T2/T3 全过；门禁 474+299 全绿，kernel 零警告；本地提交未 push。
- r410（2026-10-09）：Windows 驱动挖掘——TA ISA 字段语义（离线）：/opt/MTT-driver-only/ 为纯二进制（无头文件/文档），语义来自 Linux UMD 反汇编 FUN_00169240；TA 缓冲 40B（5 qwords）/72B（9 qwords）条目，render_ctx+0xb6 指针推进；Q0 地址/标志、Q1 param_1+0x10、Q2 打包维度、Q3 lVar29+8、Q4 维度乘积、Q5-Q8 scissor/viewport；RGXPrepareTA 回读验证布局；Linux 桥缺 360B VA/size/DMA 映射/条目构造；真实 TA 包前置 4 项；纯离线零硬件；本地提交未 push。
- r412（2026-10-09）：真实 TA 活体实现完成、trial 阻塞（最高风险轮）：测试钩子 pvr_cmd_ta_real_test（桥 0x82:0xFE，未提交；360B kmalloc→mt_ta_entry_simple_build 64×64→pvr_translator_bo_write 至 BO[10]@4096（VM 已 seal，复用已映射 BO）→mt_bridge_submit_ta_work 真实路径（MT_TA_REAL_PACKET=1 测试构建）→pvr_ta_wait_complete 等固件完成）；用户态 ta_real_test3（INIT(2)→Connect→Create(0x12)→Test(0xFE)）；DM 布局 VA @+0x28/size @+0x30 仍 [INFERRED] 待验证；活体被 trial 未建立阻塞（pvr_session_acquire 要求 trial.pinned&&connected；原版桥 git stash 验证同样失败，非本轮所致；r407 观察器不需要 trial 故未暴露）；编译零警告，pre-live T1/T2/T3 全过，无 oops/WARN/hang，测试修改已 revert；下一步 P0 诊断 trial 重建；本地提交未 push。
- r414（2026-10-09）：真实 TA 首次活体执行成功（最高风险轮）：补加 `case 0xFE` 分发（r413 卡点，r404 后空白字符 exact-match 解决）+ `pvr_cmd_ta_real_test` 钩子（r412 hook.c 原样）；`make kernel` W=1 零警告；pre-live T1/T2/T3 全过；活体单发 `status=0`——固件 `COMPLETED (0x100)` wire=1，提交到完成 219µs；DM 布局 VA @+0x28/size @+0x30 由 [INFERRED] 转 [MEASURED]；40B 条目（64×64 dummy）被固件接受；`safe_rmmod.sh` 干净卸载，无 oops/WARN/hang；测试钩子未提交；门禁 480+299 全绿；本地提交未 push。
- r416（2026-10-09）：T2 回读验证设计与实现（离线）：第 12 个 context BO（64×64 RGBA8，create 绑定、destroy 释放）；`target_va`→TA 条目 Q0（[INFERRED]）；调试 ioctl `0x82:0xFD`（`MT_TA_READBACK_DEBUG` 门控默认 0）；userspace `mt-ta-readback.c`（PPM+像素校验）；测试 +15；门禁 503+299 全绿，kernel 零警告，反向验证通过；活体验证留待 r417。
- r415（2026-10-09）：真实 TA 路径产品化（离线）：DM 布局固化 [MEASURED]；`mt_ta_submit_real()` 生产函数落地（桥内 `#if` 门控，参数化，无 hardcode，异步返 fence）；`mt_ta_real_buffer_build()` 纯函数；BO[10]@4096 复用评估通过（VM seal，长期 fix 为 create 时专用 BO）；门控开启流程 5 前置+回滚文档化；`0xFE` 钩子确认不在生产代码；测试 +8；门禁 488+299 全绿，kernel 零警告；本地提交未 push。
- r417（2026-10-09）：回读路径测试加固（离线）：tests/ta/test_ta_readback.py +19（12th BO 生命周期/0xFD 参数校验/target_va/像素分析）、tests/c +4 函数（+326 checks）；发现并修复两处门控 latent build break（0xFD case 双门控、mt_ta_submit_real 前向声明）；新建 userspace/ta_readback_analyze.h；门禁 522+625 全绿，kernel 零警告，反向验证 4 项通过；本地提交未 push。
- r419（2026-10-09）：Q0 是纯 flags、地址在 Q1（离线反汇编）：FUN_00169240:44213 初始构造无地址位，:44317 Path B 纯 flags carry-forward，Q1 低 48 位=*(param_1+0x10)才是目标地址（:44300）；r418 把 VA OR 进 Q0 污染 flags 致固件超时，r414 Q0=0 则 219us 成功；修正 mt_ta_entry_simple_set_target()：Q0=flags only、Q1=va&0xFFFFFFFFFFFF；r417 C 测试 2 处断言同步修正；门禁全绿，kernel 零警告；本地提交未 push。
- r418（2026-10-09）：双门控回读活体（最高风险轮）：双门控构建 W=1 零警告，pre-live T1/T2/T3 全过；`mt-ta-readback` 真机单发首轮 `pvr_in` 报 -EINVAL，dmesg 定位到真实 ABI bug——`mt_pvr_ta_readback_in` 内核侧未 packed（24B vs userspace 20B），r416/r417 离线测试未捕获；修复 1 行后 0xFD 全路径执行，提交成功但固件 5s 超时（ETIMEDOUT）；r414（Q0=0）219µs 完成 vs 本轮 Q0=`va|0x48000000000`[INFERRED]超时，Q0 编码疑错，不做盲探；12th target BO 活体绑定确认（0x7b000000）；pending fence 致 bridge ref=1，safe_rmmod 正确拒绝未强卸，待用户冷重启；门控已 revert，仅保留 packed 修复；门禁 522+625 全绿；本地提交未 push。
- r420（2026-10-09）：Q0/Q1 修正测试加固 + T4 纯净性门禁（离线）：新增 21 Python 测试（tests/ta/test_q0_purity.py T4 门禁 3 tests：Q0 源码扫描禁 OR/address；tests/ta/test_q0_q1_bitfields.py 15 tests：flag 位/Q1 mask/三态历史；tests/ta/test_ta_real.py DM 布局回归 3 tests）；修复 mt_marker_fence.h stale [INFERRED]→[MEASURED] 注释；T4 反向验证（注入 r418 污染→FAIL）；门禁 543 Python + 630 C 全绿，make kernel W=1 零警告；本地提交未 push。

- r423（2026-10-09，离线）：Header-only 方案——`mt_ta_real_buffer_build()` 改写：n_entries 必须为 0，仅 `TA_buf+0x10=target_va`，其余零；`mt_ta_submit_real()` 验证同步；新增 T5 Header 完整性门禁（5 tests）；存量 C/Python 测试更新；门禁 548 Python + 1054 C 全绿，kernel W=1 零警告；反向验证通过。

- r424（2026-10-09，离线）：Header-only 边界加固 + T5 门禁增强——C 扩展 `test_ta_real_buffer_build_target`（w/h 0x8000 边界拒收 0x8001、target_va 非对齐/超 48 位原样存储 [TO-VALIDATE] 不加新拒收、memset 幂等）；T5 新增 `test_header_write_whitelist`（buffer_build 内仅 `MT_TA_BUF_HDR_TARGET_VA` 可写，<0x160 其他偏移→FAIL，白名单制）+ `test_submit_real_no_direct_buf_writes`（submit 路径禁 `ta_buf[]` 直写，必须经 buffer_build）；T5 反向验证注入 `buf+0x28` 污染→精确 FAIL；门禁 550 Python + 1416 C 全绿，kernel W=1 零警告。
