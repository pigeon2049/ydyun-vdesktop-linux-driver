# S3000 vGPU 驱动适配 —— 阶段进度快照

**快照时间**：2026-10-05
**仓库**：`/opt/ydyun-vdesktop-linux-driver`（分支 main）
**对应提交**：`e9dd6a7`（内容基线：最后一次改动本文件快照内容的提交；
纯文档整理提交若未动本文件，不推进该指针，避免 amend 死循环）
**新 agent 入口**：先读仓库根 [`STATUS.md`](STATUS.md)，再读本文件对应章节。
**硬件**：Moore Threads S3000，PCI `1ed5:0222`，Debian 13，kernel `6.12.107+deb13-amd64`

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

## 5. 下一步（按序；r157–r163 后更新）

1. **真实绘制 CCB**：r148/r149 已证明 check-only kick 可在 legacy 与
   `drm_major=2` 路径经真实 DM2 空 marker 完成；离屏 fill 的非零 CCB 窗口
   已在 fabricated major 2 blit 中取得并归属（`0x8000f44000`/`0x1200`，
   39B 全定位，`+0x10`/`+0x28` 与 `SubmissionCmdGenerate` 吻合，`+0x40`
   为 job 计数器；r157–r163）。仍缺：第二份独立 CCB 样本（copy 系 producer
   在 fabrication 下同点 abort，不可达）与翻译范围的最终确定。
2. **同步 update 语义**：布局/可见性/完成条件已由核对过的 UMD 离线确定
   （r159：`flag&2` 条目、sync-block 句柄+相对偏移、先写回后交 fd），
   `flag&2` 来源与活体验证待可重建会话；未验证前不视为已支持。
3. **DDK2 TA/CDM 专属提交**：`0x82:0xC` 与 `0x81:0x5` 仍是 S4 真提交边界；
   空 marker 结果不推及这些路径。真实工作包与输入规约确认后再推进。

长期项：快照 §7 的门禁可复现（`build/` 产物入 git）与 in-tree 构建外移。

## 6. 质量门禁现状

| 门禁 | 结果 |
|---|---|
| Python 测试 | **269 项通过，1 skip**（r88 TDM 5 项；r126 首帧 envelope；r141–r143 DDK2 建销；r152 multicore；r155 shared backing；r157 DRM major；r158/r160 CCB resolve；本轮重跑全绿） |
| C RAM 模型测试 | **272 checks**（r157–r163 复核全绿） |
| 内核构建 | `W=1` 0 error / 0 warning |
| ABI 门（`mt_guest` 共享结构 + 7 结构 pahole 摘要） | PASS |
| 节点探针 `pvr_node_probe` | 0 failing step、0 value mismatch |
| 内核 dmesg | 零 WARN / BUG / Oops |
| 伪造模式回归 | 仍复现历史 4 步全 0 |

**门禁文件**（每一个都做了**反向验证**，注入 bug 确认能被抓到，再还原）：

| 文件 | 项数 | 守住什么 |
|---|---|---|
| `test_pvr_pmr_lifetime.py` | 11 | PMR 引用计数、mmap 不重复加锁、释放只能走 `pvr_pmr_unref`；`0x6:0x3/0x6:0x4` 各有独立 handler 且探针覆盖 |
| `test_pvr_heap_index_field.py` | 6 | `heap_index` 而非 `heap_config_index`；探针按名查找 |
| `test_pvr_heap_table_geometry.py` | 7 | 压掉空槽；count 由实际存入数派生；厂商蓝图逐项核对 |
| `test_windows_heap_table_decoded.py` | 10 | 从驱动二进制解表并逐字比对；MMU mode 差异；物理表与 PVR 蓝图不再混用 |
| `test_pvr_heap_name_evidence.py` | 5 | 厂商蓝图上的 PDS/USC 槽位与桥接初始化一致 |
| `test_pvr_kick_packet.py` | kick 包 + inspect | `0x88:0x4` 84 字节字段偏移（编译期 offsetof）与两次真实捕获；inspect 路径只用结构体、无裸偏移读、失败只降级 |
| `test_pvr_session_ops.py` | bind-path prereqs | 40 位 mask 显式设置；无符号表机制（`__symbol_get` 不可用，不断言 export） |
| `test_live_tqx_dma_source.py` | DMA 源 | TQX DMA-source 路径的 IOVA/GPU-PA 分离 |
| `test_pvr_tdm_shmem.py` | 5 | `0x89` TDM 共享内存桥（r88；离线实现，未加载） |
| `test_pvr_tdm_context2.py` | 4 | TransferContext2 建销与 token（r150） |
| `test_pvr_multicore_info.py` | 3 | `0x1:0xc` 回显 caps、单核（r152） |
| `test_pvr_translator.py` | 6 | translator 默认关闭/check-only 路由/update fence 后写回/期望值记录/sync-block 跟随（含反向；r126/r147–r148/r159） |
| `test_pvr_ddk2_render2.py` | 5 | DDK2 render 建销（r142；OUT 以活体 12 为准） |
| `test_pvr_ddk2_kicksync2.py` | 3 | DDK2 CCB 建销（r143） |
| `test_pvr_drm_major_gate.py` | 3 | 桥 `drm_major` 参数选路（r134；只读，不加载） |
| `test_pvr_heap_layout.py` | 2 | 堆几何基础断言 |
| `test_pvr_shim_drm_major.py` | 3 | shim major 默认 1/opt-in 2/非法值回退（r157） |
| `test_pvr_shim_shared_backing.py` | 1 | shared backing 别名/隔离 + 提交前 snapshot（r155） |
| `test_pvr_shim_ccb_resolve.py` | 1 | CCB VA→PMR 归属 + runs 形状 + 越界/反向（r158/r160） |
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

## 11. 下个硬件窗口的验证清单（按序；上一版三项已全绿）

上一版清单已完成：① DMA mask 显式化（bA43 代码 + r46 重启首绑核验 40）；
② DMA 回读比对（r49 GPU-PA 窗口转换 + TQX 回读成功）；
③ 新 probe 构建上机（r47/r51 新构建已加载建会话，非“未加载”）。

当前清单：

1. **真实绘制 kick 的非零 CCB 观察**（r56 点名的缺失输入）：
   非零 counts 已复现（fabricated r72 + 活体 r73）；
   update 侧与非零 CCB 内容仍待完整绘制路径——Rogue2D 系推进到
   TransferContext 创建即止步（legacy-TDM 疑 vendor 死代码，r112），
   真绘制大概率需新 DDK 路径；只要只读观察，不提交 GPU 工作。
2. **Translator T3 的 DM 队列格式**：改从 UMD 侧反推（r82–r84、
   r114；`mtkm64.sys` 已排除）；check-only 首帧翻译设计已完成
   （r113），待新会话执行；在拿到非零 CCB 之前不写骨架。
3. 对象存储已满：需空存储的实验（含再次的 `live_3d`）会被 `-EBUSY` 拒绝；
   下一次需空存储的实验必须等新会话（重启 + 重建），不能插队。

## 12. 运行态（2026-10-06 更新；本节是活页）

- **r170 L4 全绿（GDB 监督；批准执行；freeze 继续）**：rung5 syncprim、rung6 kicksync 建销、rung7 compute 建销、rung8 `RGXKickSync→0`（inspect，无 GPU 工作）逐级全绿，四轮 trace 全 ret=0，probe ref 稳 1，dmesg 干净。rung5 standalone 崩溃仍在；监督非常规但调用/桥一致。见 `reports/r170-supervised-ladder.md` + rung8 trace。
- **r169 字节级无罪（批准执行；freeze 继续）**：10 组桥 OUT 全量对比，18 处差异全为调用方指针回显；另否 argv[0]/重试（rung5 standalone 0/20，GDB 5/5）；遮罩机制未解释。会话健康（probe 1/bridge 0）。见 `reports/r169-byte-exoneration.md` + 失败 trace。
- **r168 tid 猎杀（批准执行；freeze 继续）**：shim 25 处加 `tid`（门禁断言+反向验证）；失败/通过轮均为单 tid，交错假设证伪，桥前缀 65/65 一致；ASLR/SMP/perturb 全排除，GDB 4/4 过，发布者未命名。会话健康（probe 1/bridge 0）。见 `reports/r168-tid-hunt.md`。
- **r167 L4 部分通过（批准执行；freeze 继续）**：手跑阶梯（桥零重载）rung1–3 全绿；rung4 先 2 崩后 4 过；rung5 standalone 11/11 SIGSEGV、GDB 2/2 过——core 验尸为 `RGXCreateRenderContextCCB+1525` 取 `r12+8==NULL`，trace 全 ret=0、内核零错误、probe ref 稳 1，桥无罪。rung6–8 被阻。见 `reports/r167-l4-partial-segv.md` + trace + 验尸笔录。
- **新会话 freeze 中（r166，批准执行）**：`mt_guest_probe` 绑定 `00:0e.0`（trial `20261005T161706Z-cf0d876e`，Guest/FW `2/2` pinned，ref 1），`mt_pvr_bridge` 默认参数在载（`card1`/`renderD128`，ref 0）；L3 全绿（node 0 failing/0 mismatch，dma smoke PASS refs 平衡）；dmesg 无新增 WARN/BUG/Oops。**不 rmmod、不 unbind、不提交额外工作。**见 `reports/r166-session-rebuild.md`。

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
