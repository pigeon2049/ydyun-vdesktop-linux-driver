# S3000 vGPU 驱动适配 —— 阶段进度快照

**快照时间**：2026-10-03
**仓库**：`/opt/ydyun-vdesktop-linux-driver`（分支 main）
**对应提交**：`39b150b`（内容基线：最后一次改动本文件快照内容的提交；
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

## 5. 下一步（按序；r79–r114 后更新）

1. **真实绘制 kick 观察**：合成零 count 与非零 check 均已复现
   （fabricated + 活体，r72/r73）；update 侧需 DDK2（r74–r77），
   而 DDK2 需桥特性开关（r78，改代码 + 重编 + 重载，单独立项）；
   非零 CCB 内容仍只能来自完整绘制路径（r56 原判不变；
   Rogue2D 系已推进到 TransferContext 创建，r86–r112，
   legacy-TDM 疑 vendor 死代码）。
   在拿到真实 CCB 内容之前不写翻译器骨架——输入规约先行，代码随后。
2. **Translator T3**：DM 队列格式改从 UMD 侧反推（r82：
   `mtkm64.sys` 系 host KMD，无 guest kick 语义，已排除）；
   TA 提交链已静态打通（KickTA→PrepareTA→SubmitTA→`0x82:0x14c`，
   r83/r84）+ fence 生成器语义（r114）；check-only 首帧翻译设计
   已完成待新会话执行（r113）。
3. `RGXCreateZSBuffer` 的 13 参数形状已摸清，但它要 UMD 内部的
   heap/context 对象，小 buffer 冒充会直接段错误。gdb 证明崩溃点在
   `MTSRVAllocExportableDevMem ← MIW` 经 libc 字符串函数：
   MIW 把 `*param_1` 当 `MemHeap_*` 名字表下标。
   要驱动它，需要先拿到真正的 psDevMemCtx/MemHeap 描述符指针，
   这是 ZSBuffer/freelist/HWRT 这一串的共同前提。
3. `RGXCreateZSBuffer` 的 13 参数形状已摸清，但它要 UMD 内部的
   heap/context 对象，小 buffer 冒充会直接段错误。gdb 证明崩溃点在
   `MTSRVAllocExportableDevMem ← MIW` 经 libc 字符串函数：
   MIW 把 `*param_1` 当 `MemHeap_*` 名字表下标。
   要驱动它，需要先拿到真正的 psDevMemCtx/MemHeap 描述符指针，
   这是 ZSBuffer/freelist/HWRT 这一串的共同前提。

长期项（不影响当前推进）：快照 §7 的门禁可复现与 in-tree 构建外移。

---

## 6. 质量门禁现状

| 门禁 | 结果 |
|---|---|
| Python 测试 | **226 项通过**（r45–r71 新增 arena/kick-inspect 等门禁；r88 新增 TDM 5 项；10-03 L1 复核全绿，本轮重跑仍全绿） |
| C RAM 模型测试 | **268 checks**（10-03 L1 复核全绿） |
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

## 12. 运行态（2026-10-03 快照刷新；本节是活页，其余章节为历史）

- 新 retained 会话运行中：`mt_guest_probe` 已绑定 `00:0e.0`（Guest/FW
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