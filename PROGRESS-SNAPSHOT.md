# S3000 vGPU 驱动适配 —— 阶段进度快照

**快照时间**：2026-10-01
**仓库**：`/opt/ydyun-vdesktop-linux-driver`（分支 main，工作区干净）
**对应提交**：`e3e75e8`（bA32 收尾）
**硬件**：Moore Threads S3000，PCI `1ed5:0222`，Debian 13，kernel `6.12.107+deb13-amd64`

本文件是**当前状态的唯一权威快照**。逐轮过程记录在根目录 `MEMORY.md`（追加式，不回改）。
两者冲突时，以本文件为准。

---

## 1. 一句话现状

厂商 MASA UMD 已经能在我们的内核桥驱动上**走完设备连接和设备内存上下文创建**，
`RGXCreateDeviceMemContext` 返回 **0**；下一个卡点是**渲染上下文创建**，
根因已定位为一个**数据问题**：我们把 `PDS Code and Data` / `USC Code` 两个堆名
绑定到了尺寸装不下它们的格子上。

---

## 2. 真机阶梯（当前真实结果，非目标）

驱动：`kernel/recovery/mt_pvr_bridge.ko`（Stage B S1，**不绑定 PCI、不碰 MMIO**）
节点：`/dev/dri/renderD128`（`mtgpu` 仍独占 `00:0e.0`，`card0` 归它，我们用独立 `pvr` 节点）

| 步骤 | 符号 | 返回 |
|---|---|---|
| 1 | `PVRSRVConnect` | **0** |
| 2 | `PVRSRVConnectionCreateDevice` | **0** |
| 3 | `RGXCreateDeviceMemContext` | **0** |
| 4 | `RGXCreateRenderContext` | **1** ← 当前墙 |

第 3 步是本轮突破点：它曾长期返回 11 / 37 / 82，并伴随用户态 `double free` 崩溃。

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

第 8 项值得单独强调：**`double free` 只是三层之外的表象**。
整条链路上一个错误码都没有暴露（50 条桥命令全部 ret=0），
是加了一行 `pr_info` 打印 UMD 实际传的参数才看出来的。

---

## 4. 当前墙：堆名与槽位的映射错误（**数据问题，不是代码问题**）

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

**两条独立证据（驱动二进制尺寸表 + 具名资源 profile）同时否定「改大尺寸」这个方案。**
正确做法是把 `PDS Code and Data` / `USC Code` 重新绑到 0,1,2,3,6,9,10 中的某两格。

---

## 5. 下一步

1. **重新推导堆名 ↔ 槽位映射**（当前唯一阻塞项）。
   仍需一步外部确认：UMD 按这两个名字查找时期望的**堆大小/用途**。
   已实测 PDS 需 ≥ `0x9000`；可用 gdb 在
   `MTSRVSubAllocDeviceMemMIW` 上取全部请求来反推。
   候选槽位 0,1,2,3,6,9,10 的 size 分别为 512 GiB / 4 GiB / 4 GiB / 16 MiB /
   4 GiB / 4 GiB / 4 GiB。

2. 映射确定后，`RGXCreateRenderContext` 应能越过 arena 分配，
   继续推进到真正的 GPU 提交（S4 阶段，需单独批准）。

3. 长期项（不影响当前推进）：
   - 目录结构与 Make 流程规范化（见 §7）
   - 门禁不可复现问题（依赖 gitignore 的 `build/` 产物）

---

## 6. 质量门禁现状

| 门禁 | 结果 |
|---|---|
| Python 测试 | **159 项全绿**（本轮 123 → 159） |
| C RAM 模型测试 | **186 checks**（170 → 186） |
| 内核构建 | `W=1` 0 error / 0 warning |
| ABI 门（`mt_guest` 共享结构） | PASS |
| 节点探针 `pvr_node_probe` | 0 failing step、0 value mismatch |
| 内核 dmesg | 零 WARN / BUG / Oops |
| 伪造模式回归 | 仍复现历史 4 步全 0 |

**新增的 4 个门禁文件**，每一个都做了**反向验证**（注入 bug 确认能被抓到，再还原）：

| 文件 | 项数 | 守住什么 |
|---|---|---|
| `test_pvr_pmr_lifetime.py` | 9 | PMR 引用计数、mmap 不重复加锁、释放只能走 `pvr_pmr_unref` |
| `test_pvr_heap_index_field.py` | 6 | `heap_index` 而非 `heap_config_index`；探针按名查找 |
| `test_pvr_heap_table_geometry.py` | 5 | 压掉空槽；count 由实际存入数派生 |
| `test_windows_heap_table_decoded.py` | 10 | 从驱动二进制解表并逐字比对；MMU mode 差异 |
| `test_pvr_heap_name_evidence.py` | 5 | 把堆名错绑登记为**显式已知缺陷** |

设计要点：`test_pvr_heap_name_evidence.py` **不**断言掉当前缺陷，
而是把它记录成一条「已知缺陷」测试。**映射被真正重推时它会失败——
那就是该重写该文件、并删除其中描述的临时手段的信号。**

---

## 7. 待办（用户已提出，尚未动手）

### 目录结构：**建议局部调整，不建议大重构**

实测数据：

| 目录 | 规模 | 问题 |
|---|---|---|
| `build/` | **5.7 G** | 全 gitignore，但**主门禁依赖它** |
| `decompiled/` | 2.5 G | gitignore，合理 |
| `tools/` `downloads/` | 882 M / 659 M | gitignore，合理 |
| `reports/` | 60 M / 421 文件 | 证据链，不宜改名 |
| `tests/` | 656 K | 50 `.c` + 23 `.py` 混放 |
| `kernel/recovery/` | 36 M | **196 个构建产物 + 52 个源码**（in-tree 构建） |

三个**真实**问题：

1. **门禁不可复现（最严重）**。`scripts/verify-runtime-integration.py` 要求
   `build/recovery-channel/loaded-6f259*.ko` 恰好存在一个，而 `build/` 是
   gitignore、git 跟踪数为 0。**在新机器 clone 上这个主门禁直接 `raise` 失败。**
   替代方案：把该 `.ko` 或其 pahole 结构摘要（几 KB，可 diff）纳入 git。

2. **内核构建 in-tree**。`M=$(CURDIR)` 让产物落进源码目录。改 `M=$(BUILD)` 即可外移。

3. **无顶层 Makefile**，5 个分散 Makefile，各自 `mkdir -p ../build/...`。

**不建议**做的：拆 `tests/` 子目录（交叉引用全改，收益低风险高）、
动 `reports/`（毁证据链）、重排 `kernel/recovery`（24 个模块有加载顺序依赖）。

### 按步骤测试：建议分四层

| 层 | 内容 | 需 root/硬件 | 时长 |
|---|---|---|---|
| **L1 纯离线** | 159 py + 50 C RAM | 否 | ~3 s |
| **L2 构建+ABI** | `W=1`、ABI 漂移、线尺寸门 | 否 | ~90 s |
| **L3 节点探针** | 加载模块、`pvr_node_probe` | 是（不碰硬件） | ~2 s |
| **L4 UMD 端到端** | 真实 UMD 阶梯 | 是 | 每级几秒 |

L4 最该做成**阶梯式**：每级一个可独立跑的用例，失败就停在那级并打印该级桥命令序列。
现在这个信息每次都要手工解析 trace 才能拿到。

### Make 流程规范化：目标接口

```
make check          # L1+L2，默认门禁，不碰硬件
make check-offline  # 仅 L1
make probe          # L3（自动 insmod/rmmod，必须用 trap 保证清理）
make umd            # L4 阶梯，逐级打印桥命令
make kernel         # 显式外移构建
make clean          # 含 in-tree 产物
make help
```

关键约束：**`make check` 绝不能加载模块或碰 PCI**；
L3/L4 必须用 `trap` 保证 `rmmod`——这正是本轮那个 `D` 态自死锁的教训。

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

---

## 9. 运行态

- `mt_pvr_bridge` 已加载，refcnt 0，无残留进程，dmesg 干净
- `mtgpu` 独占 `00:0e.0`；我们用独立 `pvr` 节点 `renderD128`
- **不需要重启**（上一轮的自死锁已随重启清除）
- 厂商 UMD 位置：`/tmp/mtt-linux-umd-5.2.0/root/usr/lib/x86_64-linux-gnu/libsrv_um_MUSA.so.1.0.0`
  （树内留档：`build/legacy-umd-pvr-connect-candidate/rootfs/usr/lib/x86_64-linux-gnu/`）
- **未决问题**：刷机版本仍未定。
  `build/official-vgpu-r22b-mmu-mapping-audit-20260929/mtgpu.ko`（最新，09-30 00:09）
  vs 已安装的 09-28 21:34 那个（sha256 `84817b6d…`，不匹配任何 93 个产物）。
  **`/lib/modules` 至今未动过。**
- **S4（真实硬件提交）仍需单独批准。**