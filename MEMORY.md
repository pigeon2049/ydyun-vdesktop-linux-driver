# MEMORY — 摩尔线程 vGPU 驱动适配

最后更新：2026-10-01（bA19：S1 验收加载模块，真实 UMD 打真实 ioctls；6 个真 bug）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（bA19：S1 验收——加载模块跑真 UMD，抓出 6 个真 bug）

接 bA18。用户选 A（insmod + 直通验收 + rmmod）。**已加载、已验收、已卸载复原**。

### 0. 先纠一个会带偏整轮的错判

- `/lib/modules/6.12.107+deb13-amd64/updates/mtgpu.ko`（20 MB、带 `debug_info`）
  **是本项目自己适配阶段构建的产物**（`src/mtgpu-2.7.1-6.12` + `patches/`），
  不是厂商原版二进制，**不能当独立 oracle**。已核对：其 build-id
  `ee63001f…` 与 `build/` 下 93 个 mtgpu.ko 都不匹配，但树里就是同一份源码。
- 但**厂商源码本身在树内**（`inc/pvr/include/pvr_drm.h`、`generated/*_bridge.h`），
  这些是 UMD 的 ABI 权威，可以直接引用——而且本次两个关键 bug 正是靠它定死的。

### 1. 验收前先加"只记录不伪造"直通模式

- `probe/umd_bridge_shim.c` 增 `UMD_SHIM_PASSTHROUGH=1`：open/ioctl/mmap 全部转真内核，
  什么都不代答；默认伪造模式**逐字未变**（bA1–bA14 全部离线结果依赖它），回归 4 步全 0。
- 新增 `probe/pvr_node_probe.c`：直连节点逐条打命令，把 UMD 从链路里摘出来定位。
  写它时自己踩了两个坑，都已修正并注释：
  - `ioctl` 失败返回 -1、原因在 `errno`；原写法 `strerror(-ret)` 对每步都显示 EPERM，
    把真实错误全掩盖了（这就是 EFAULT 藏了好几轮的原因）。
  - 期望被拒绝的步骤（ENOTTY/EINVAL）原先计入失败，使"正确的拒绝"看起来像 bug。

### 2. 6 个真 bug（全部由真机验收抓出，离线测试一个都测不到）

1. **`FOP_UNSIGNED_OFFSET` 缺失**（DRM 6.12）。`drm_open_helper` 在
   `drm_file.c:312` 直接 `return -EINVAL`，`pvr_open` 一次都不会被调用。
   加 `.fop_flags = FOP_UNSIGNED_OFFSET` 后节点名返回 `pvr`（UMD 唯一接受的名字）。
2. **ioctl 参数被二次拷贝**。6.12 `drm_ioctl()` 先把参数拷进内核 `stack_kdata`
   再调驱动；我们又 `copy_from_user(raw)` → 对内核地址拷贝 → 必 EFAULT。
   厂商 `PVRSRV_BridgeDispatchKM` 是直接 `psSrvkmCmd = (void *)arg` 不再拷。
   只有 `in_ptr`/`out_ptr` 仍是用户指针，由 `pvr_in/pvr_out` 校验。
3. **INIT 的 `_IOC_SIZE` 是承重字段**。厂商写的是
   `DRM_IOW(0x45, struct drm_pvr_srvkm_init_data /* __u32 */)`，编码后正好 `0x40046445`。
   写成 `_IO` 则 size=0，DRM 一个字节都不拷，驱动读到未初始化栈。已加 4 条 `static_assert` 钉死。
4. **句柄被提前左移**（bA15 引入的回归）。`AcquireInfoPage` 依
   `PVRSRV_BRIDGE_OUT_ACQUIREINFOPAGE` 应返回 `IMG_HANDLE hPMR`，**不是** mmap 偏移；
   UMD 自己会 `<<12`。返回偏移后，UMD 把该值原样回灌 `PmrLocalImportPmr` 的
   `hExtHandle`，与 PMR 表永远对不上 → `-ENOENT`。
   同类错还有 `PmrLocalImportPmr` 的 `hPMR`、以及把 `handle<<12` 塞进
   `AllocSyncPrimitiveBlock` 的 32 位 `ui32SyncPrimVAddr`（那是 VA，不是句柄）。
   **教训：bA15 的"mmap 偏移 == handle<<12"是 UMD 侧行为，不能倒推成内核返回值。**
5. **mmap 完全缺失**。fops 里没有 `.mmap`，UMD 映射 info 页无从谈起。
6. **mmap 三个连环坑**：
   - 边界算错：`offset + length > pmr->bytes`，而 `offset` 是句柄左移 12 位（远大于
     PMR），导致任何整 PMR 映射都被拒。应为 `length > pmr->bytes`。
   - `remap_vmalloc_range()` 导出了但要求 `area->flags & VM_USERMAP`，`vzalloc()`
     不设该标志 → `-EINVAL`；想设它得用 `__vmalloc_node_range()`，但 modpost 报
     **undefined**（`mm/` 内部符号）。`remap_vmalloc_range_partial()` 同样未导出。
     最终用**已导出**的 `remap_pfn_range()` 逐页映射。
   - pgprot：`PAGE_KERNEL` 的 `_PAGE_USER` 位为空，映射出来用户态读不了，
     表现为 `segfault at info_page+0x48`。须
     `__pgprot(pgprot_val(PAGE_KERNEL) | _PAGE_USER)`。

### 3. 验收结果

- `pvr_node_probe` **全绿**（init_module=1 与 2 都过）：INIT / Connect
  （`bvnc=0x23000406600017`，与 bA16 对 Windows 核对的期望值一致）/ 事件对象 / info 页 /
  `HeapCfgHeapCount=11` / `General`、`USC Code`、未命名槽返回空串 / 越界 `-EINVAL` /
  `AllocSyncPrimitiveBlock` / 未知命令 `-ENOTTY` / **info 页真能 mmap 并读出内容**。
- **真实 UMD（5.2.0 libsrv_um_MUSA）打真实 ioctl：执行的桥命令从 4 条涨到 12 条**，
  走完 Connect → info 页 mmap 成功 → 多条 SRVCORE/MM 命令 → 主动 Disconnect。
  崩溃点从"mmap 失败后读 0x32"变成"往 0x58 写"，说明 info 页已被真正消费。
- 新发现：`0x86:0x4 RGXACQUIREHWPERFFSETTINGS`（在 19 条内，已支持）与
  **`0x86:0x5`（不在 19 条内，回 `-ENOTTY`）**——UMD 要继续就必须实现它，归 S2。
- 门禁：Python 120 项全绿；`verify-runtime-integration.py` 通过
  （`pvr_bridge_core_test` 170 项 RAM 检查 + `W=1` + 7 结构 ABI 门禁）。
- **已 rmmod 复原**：`mt_guest_probe` 回到 62 引用、taint 仍 12800、只剩 `card0`、
  00:0e.0 仍归 `mt_guest_probe`、无 oops/WARN。

### 4. 过程中的教训（都是本轮真踩）

- **`make ... 2>&1 | grep error` 会吞掉 modpost 失败**：连续几轮我加载的都是旧
  `.ko`，却以为在测新代码。判断模块是否真的是新的，要看 `strings` 里有没有新字符串，
  不能只看 `grep error` 没输出。
- 反编译/静态材料不如**树内的厂商源码**权威：本次两个致命细节（ioctl 尺寸位、句柄
  vs 偏移）都是 `pvr_drm.h` / `generated/*_bridge.h` 一眼定死的。
- 自造模块当 oracle 会自欺：拿自己的 `mtgpu.ko` 去"验证"自己的桥接，等于自己判自己卷子。

### 5. 下一步（S2）

`0x86:0x5` + 0x58 写故障定位；再往后才是 `0x82:0x8` render context 之后的
PMR 分配族。**S4（真实硬件提交）仍需单独批准。**

---

## 目标

在 Debian 13.7 / Linux 6.12.107 虚拟机中，为摩尔线程 S3000 vGPU
（PCI `1ed5:0222`，subsystem `1ed5:1101`，Guest 设备 `0000:00:0e.0`）
自研 Linux 原生驱动，最终实现可用的图形加速（GL/Vulkan/合成器）。

当前显示仍为 QXL + llvmpipe 软件渲染。GPU 硬件通路已打通并可执行任务，
但距离可用图形栈还差关键环节（见「关键阻塞」）。

---

## 本次会话进展（bA18：Stage B S1 模块写好（未加载），未提交）

接 bA17。环境确认：**root 可用**（`sudo -n true` 成功，与 r43 不同）、
`mt_guest_probe` 在跑并持有 `00:0e.0`（62 引用）、taint 12800、当前无 MTT 节点。

### 1. 新增

- `kernel/mt_pvr_device.h`：连接对象布局（UMD 读取点逐偏移 `static_assert`：
  `+0x00` srv handle、`+0x08`、`+0x0c` pid、`+0x14` flags、`+0x28` info 页、
  `+0x48` TL 流、`+0x50` HWPerfUm、`+0x60` HWPerf 设置、`+0x70` refcount、
  `+0x78` devmem ctx、`+0xa0` features、`+0xb0/+0xb8` sync arena）、
  feature 块**按偏移访问**（不臆造未命名字段）、Connect 结果、info 页构造。
- `kernel/recovery/mt_pvr_bridge.c`：DRM 节点 `.name="pvr"`；**ioctl 号写死**为
  UMD 实际用的 `0xc0206440` / `0x40046445`（DRM 6.12 的 `DRM_IOCTL_DEF_DRV` 只认
  `DRM_IOCTL_*` 宏、不自动编号，编号错等于没有这个节点）；19 条命令分发；
  PMR 系统内存后备；句柄来自真实分配器；堆几何来自计划表；堆名回写调用者缓冲
  （bA5 的教训）。
- `kernel/mt_pvr_wire.h` 增补 `0x6:0xf` ctxcreate 的 IN/OUT（4/24 字节）。

### 2. 过程修正（均由编译/测试抓出）

- DRM 6.12 的 `drm_driver.open` 返回 `int`（不是 `drm_device *`）、
  `gem_prime_import` 返回 `struct drm_gem_object *`；stage 1 不用 GEM，直接去掉。
- `conn` 必须 packed 才能对上 UMD 偏移；用显式 pad 字段填满每段间隙。
- **反编译不可靠的一处**：`ConnectionCreate` 的 `V == strtol("4")` 与
  `top16 == 0x23` 数学上互斥（`(bvnc>>32)&0xffff` 对 `0x0023000406600017`
  恒为 `0x23`），而该值实测通过 ⇒ Ghidra 此处还原有误。单测只钉已验证的三个
  子门，不把这条写进门禁。

### 3. 门禁与状态

- `tests/pvr_bridge_core_test.c` 扩到 **170 项**（新增 device 布局/Bvnc/feature/
  info 页），全量 Python 120 项全绿；`verify-runtime-integration.py` 通过
  （含 `W=1` 内核构建与 7 结构 ABI 门禁）。
- 静态核验：模块内**无** `pci_register_driver`、无 `ioremap`/`readl`/`writel`、
  无 BAR 申请 ⇒ 加载它不触碰主模块活会话。
- **未执行 insmod**（按纪律需用户确认）。下一步：切「只记录不伪造」shim 验收。

---

## 上次会话进展（bA17：Stage B S0 落地（线格式 + 队列核心 + 门禁），未提交）

接 bA16。开始写 Stage B 桥的内核侧代码（纯逻辑，不碰硬件）。

### 1. 新增内核头（可离线验证）

- `kernel/mt_pvr_wire.h`：阶段一命令的线上结构（`__attribute__((packed))`，
  逐字段 `static_assert`）。三处 **UMD 线格式 > 5.2 头声明**的差量显式建模为
  保留尾部（`0x6:0x9` IN 72/OUT 24、`0x6:0x13` IN 32），避免内核截断驱动写入的字段。
  头文件自带类型定义（照 `kernel/mt_mmu.h` 惯例），模块与用户态测试都能编译。
- `kernel/mt_pvr_queue.h`：
  - **令牌 + 双环**（照 bA16 的厂商模型）：提交记录 64 槽（token@+0x08、
    flags `0x20`=可抢占、context、哨兵），完成事件 64 槽（token@+0x08、
    type@+0x04）；`head`=下一写位、`tail`=下一读位、保留一槽（容量 N-1）。
  - **事件环无条件排空**，只有令牌匹配才推进提交侧；不匹配即陈旧事件丢弃
    （与 `FUN_14000be34`+`FUN_14000e6a4` 一致）。这一条最初写反，
    由 RAM 测试抓出并改正。
  - 环满回 `-EAGAIN`（厂商是自旋 10000 次后静默丢弃，不学）。
  - **mmap 偏移编解码**：`offset == handle << 12`（bA15 实测 28/28 成立）。
  - 句柄分配器：单一单调空间、基址 `0x1000`、零句柄永不出现（bA4 教训）。
  - **堆表服务端**：`mt_pvr_heaps_init/find/count`，几何来自
    `mt_guest_plan_heaps()`，名字表四名（General/PDS Code and Data/USC Code/
    Component Control），未命名槽不匹配空串与前缀。

### 2. 测试与门禁

- `tests/pvr_bridge_core_test.c`：**133 项 RAM 检查**（ASan+UBSan），覆盖
  逐字段偏移、令牌单调与不匹配、陈旧事件、环满/回绕、fault 不得报 drained、
  偏移编解码、句柄唯一性、堆表几何与按名查找。已注册进
  `scripts/verify-runtime-integration.py`（随全量门禁跑）。
- `tests/test_pvr_wire_sizes.py`：4 项门禁，**C 结构尺寸 ↔
  `stage-b-bridge-requirements.json` 互为门禁**（含三处 wire 差量断言）。
- 全量：`python3 -m unittest discover -s tests` **120 项全绿**；
  `python3 scripts/verify-runtime-integration.py` 通过（含 `W=1` 内核构建与
  7 结构 ABI 门禁）。**未加载任何模块，未访问硬件。**

### 3. S0 状态

设计文档 §8 的 S0 已完成。下一阶段 S1：新 recovery 模块（DRM 节点 version 名
`pvr`）+ 把 19 条命令接到真实 ioctl，用「只记录不伪造」模式的 shim 验收。

---

## 上次会话进展（bA16：Windows 原厂 KMD 交叉核对，未提交）

接 bA15。用户要求回 Windows 反编译与原始驱动做调研，三路并行 + 亲自复核。

### 1. 堆表闭环（新硬证据）

- 新脚本 `scripts/dump-windows-heap-table.py`：从 `mtkm64.sys` 的 `.data` 读出
  `FUN_14001ccd8` 用的 **22 项** GPU-VA 堆表（MMU 模式 0 选 `0x141030d90`，
  模式 1 选 `0x141030fa0`；`decompiled.c:23169` 定 22 项），
  与 `mt_guest_plan_heaps()` 逐项对照，落盘 `reports/windows-heap-table-22.json`。
- **我们填的 9 个非空堆全部逐字节一致**（含此前记为「无法比较」的 3/7/8/9），
  4/5 两堆双方都空；7 项单测 `tests/test_windows_heap_table.py` 门禁。
- 13 项资源 profile + 掩码 `0x1ef9`（启用）与 `0x1220`（延后）亦与 Windows 一致。

### 2. 两处旧报告勘误（已加注记，不改历史结论）

- `windows-resource-mapping-audit.md`：`gpu_device+0x24` 是 **IO 窗口槽计数**，
  **不是堆数**；Windows 堆表 22 项、资源 profile 13 项。「4 heaps」应改读为
  「4 个 IO 窗口槽」。
- `windows-fw-heap-ring-investigation.md`：`0x1800000` 在 `mtkm64.sys` 里是
  **24 MiB 私有池大小**（`disassembly.txt:41878`），与 Host 侧 OSID 步进
  巧合同值、语义不同；且 **Windows 不做 per-OSID 堆**（`osid` 全文零命中）。

### 3. 完成信号：厂商模型 = 令牌 + 双环比对（已回灌 Stage B §7.4）

- 驱动**不自增 fence**：FenceID 由 OS 分配 → 写入 `0x98` 字节工作记录 `+0x08`
  → 固件完成时原样回填到 `0x18` 字节完成事件 `+0x08` → KMD 只**比对**。
- 每队列块 `0x40`：`+0x04` lastPrepared / `+0x08` lastCompleted / `+0x14` 提交尾 /
  `+0x20` 记录环基址 / `+0x28` 完成头 / `+0x2c` 提交尾；记录 flags `0x20`=可抢占。
- 固件环：每 DM 块 `0x2e30`，命令环 64×`0x50`、完成事件 64×`0x18`，
  head/tail `&0x3f` 单生产者单消费者。提交环满：自旋 10000 次后**静默丢弃**
  （我们应回 `-EAGAIN`）。
- 无 per-fence `KeSetEvent`；靠一次完成一次回调唤醒。
- ⇒ Stage B §7.4 已按此重写：完成环直接复用 `kernel/mt_fw_event*`。

### 4. 上下文规模与新线索

- Windows 侧每上下文状态区 **96,000 字节**（`decompiled.c:24529-24530`），
  与我们 r36 自造的 ~84.3 KiB 同量级；`mext` 扩展上载 `0x800`/引擎。
- 新线索：`FUN_140019cc4`（`decompiled.c:20383`）向 `param_3+0xb0`（每槽 `0x40`，
  3 个 qword）与 `+0xf0`（9 项）写**成组上下文寄存器常量**，是宿主侧唯一的
  定值寄存器表（驱动未命名）。S3 审包时作对照物。

### 5. 明确查不到（别再耗时间）

CSW/Context-Store/Load 的寄存器名与写序列、`Serial-Kick`/`POLL_MAX_COUNT`
等固件侧参数、`DAT_1402a6af0`（`.rdata` 未导出）——全部只在未反汇编的固件 blob
（`0x141042000`–`0x1411b7000`）里。要拿需单独做一次该区间的 Ghidra 分析。

### 下一步（待用户拍板）

Stage B 按 §7.4 新模型落 S0（纯内核单测：令牌分配器、双环、offset 编解码）。

---

## 上次会话进展（bA15：Stage B 设计文档（评审稿），未提交）

接 bA14。用户拍板「先做 Stage B 设计文档」。

### 1. 新增产物

- `reports/stage-b-kernel-bridge-design.md`：Stage B 评审稿（目标/范围/ABI 事实/
  19 条阶段一命令规格/kick 第二波/内核模块设计/验证与回退/风险/未决问题）。
- `scripts/build-stage-b-bridge-requirements.py` + `reports/stage-b-bridge-requirements.json`：
  机器可读需求表（205 条命令名 + IN/OUT 字段偏移 + 线格式差量 + 19 条已观测）。
- `tests/test_stage_b_bridge_requirements.py`：10 项单测钉住表结构与 trace 不变式。
- `reference/kmd-5.2.0-server-generated/`：5.2.0 KMD 生成头（22 个 + `mt_bridge.h`），
  从易失的 `/tmp/opencode/kmd52` 复制留档（含 PROVENANCE 与包 sha256）。
- `reports/umd-bridge-sync-trace.jsonl`：bA13 成功会话轨迹入库（112 行）。

### 2. 本轮新确认的硬事实

- **mmap 偏移 = 本地句柄 << 12**：4 份 trace 的 28 次 mmap 全部成立且 4KiB 对齐。
  KMD 侧约束：凡返回给 UMD 的可映射句柄 `h`，`h<<12` 必须是该 PMR 的合法 DRM 偏移。
- **UMD 线格式 > 5.2 头声明**（单测钉住）：`0x6:0x9` IN 72/OUT 24（头 68/20）、
  `0x6:0x13` IN 32（头 28）。内核必须按 UMD 尺寸接收。
- **勘误**：CONNECT OUT 头文件即 17B（`ui64PackedBvnc@0/eError@8/
  ui32CapabilityFlags@12/ui8KernelArch@16`），与 UMD 实发一致——bA14 初稿误记为 16，
  由单测抓出并改正。
- 连接对象关键偏移（bA13 实测确认）：`+0x14` flags、`+0x28` info 页、`+0x48` TL 流、
  `+0x50` HWPerfUm、`+0x60` HWPerf 设置、`+0x68/+0x70/+0x78`、`+0xa0` 特征块、
  `+0xb0`×2 sync arena。
- 堆几何以 `kernel/mt_guest_heaps.h:mt_guest_plan_heaps()` 为准（11 项含 2 个空堆），
  shim 里那张是早期近似，勿照抄。

### 3. 仍缺头文件的三条命令

`0x88:0x5/0x6/0x7`（RGXKICKSYNC 变体）在 2.7.1（止于 +4）与 5.2 Host 包（无 RGX 组）
里都查不到，只能真机抓取或补 5.2 Guest 头。

### 下一步（待用户拍板）

按设计文档 §8 推进，建议从 S0（纯内核单测：句柄分配器 + offset 编解码 + 堆表回填）
与 S1（加载模块但不绑硬件，用「只记录不伪造」shim 打真实 ioctl）起步。

---

## 上次会话进展（bA13：Windows 灵感回灌 + Sync 实测突破，未提交）

接 bA12。用户建议回到 Windows 原始驱动找灵感，并行三路 mining + 活 harness 验证，
推翻两个旧结论，打通一条新链。

### 1. bA12 勘误：kick 崩的不是事件过滤器缺失，是首参传错

- `MTSRVGetClientEventFilter(param_1,1)` 的 `param_1` 是 **psDevConnection**
  （`+0x50` 为 HWPerfUm 指针，`FUN_0013ede0` 在 `ConnectionCreate→_AcquireHWPerfSetting`
  后分配；`conn+0x14=0x20` 时 bit `0x10` 为零必走该路）。`RGXKickTA/RGXKickCDM` 的
  param_1 同为连接，`+0x30` 检查的是 kick 参数块——bA12 把 renderctx 当 param_1
  传，野读是调用错误，不是 UMD 对象图缺失。
- 实测：`PVRSRVConnect` 后 `conn+0x50` 非零；`RGXKickTA(conn, kickbuf)` 越过
  `GetClientEventFilter`，崩点推进到 `RGXPrepareTA(FUN_00178800)+0x253`
 （kick 结构字段未整形，预期内）。

### 2. Sync memType 动态值首次捕获：`0x100000000`，静态 2 理论作废

- `0x02:0x00` 输入 8B = `00000000 01000000`（u64 `0x100000000`），对照 5.2 头即
  `ui64MemType`。静态 `edx=0x2` 只是 RA 创建时的 arena 类别参数
 （`FUN_001a0020→FUN_0019da20` 的 param_3），经 `RA_Alloc` 的是 `param_4` 掩码值；
  `CreateSyncPrim` 调 `RA_Alloc(...,1,0x100000000,...)`，掩码保留 bit32。
- 对照试验证明因果：去掉手动 `CreateSyncPrim` 的会话零 `0x02:0x00`、零 DebugPrintf；
  加上即现。bA1/bA5 的「memType 恒为 2」降级为已证伪。

### 3. `CreateSyncPrim` → 0（device-conn 真 sync ctx + 新伪造）

- 会话配方（`build/probe/umd_connect_harness` 单进程）：
  `connect 0 → PVRSRVConnectionCreateDevice(b7,u0,u0) → RGXCreateDeviceMemContext(b7*,b5,b5+8)`
  → `params+0x10=devctx`（devctx 指针**直接**存，不是子结构；bA6 修正）
  `+0x30/+0x34≠0 → RGXCreateRenderContext(b7*,b6,b9) → 0，out 非零`。
- sync ctx 三槽皆有效：`conn+0xb0` ×2、`renderctx+0x30`（arena 皆非零；
  中间一次「arena=0」是 harness 引用号引号包错的人为乌龙）。
- `CreateSyncPrim(device-sync,...)` 返回 3 的根因是伪造质量：
  全零 `0x02:0x00` 输出使 `BlockSize=0`，后续 `RA_Alloc` 入口拒零（`0x652`）。
  shim 新增 `fabricate_sync_alloc`（句柄/`0x5000+` PMR/`BlockSize=0x1000`）后
  **首次返回 0**，out 非零；连带捕获新桥 `0x02:0x07 BridgeSyncAllocEvent`。
- 给 Stage B 的硬输入：Sync 分配真值就是 `0x100000000`；`0x02:0x00` OUT 必须带
  非零句柄/PMR/BlockSize，否则 UMD 自己的 RA 二次分配就地失败。

### 4. Windows 侧灵感（结论性，细节见报告 §20）

- Windows 四件套无 `RenderContext/KickTA/PSC` 字面量——对应物是 WDDM Cb
  （`D3DDDIRenderCb/CreateContextCb/CreateSynchronizationObjectCb…`，
  `mtdxum64.dll`）+ `musa::compiler` LLVM 后端
  （`llvm.musa.load.vertex.buffer` 等，`mtgfxc64.dll`）。PSC 编译器只在 Linux UMD
 （`001a4fc0…`）；顶点取数手写重实现无望，**桥接路线是唯一活路**（强化 Stage B）。
- `mtkm64.sys` 内嵌 FW 日志给出固件侧 DM/kick/event 全家桶词汇；
  `InitMTFeatures` 按 device-type 选四套特征表（Sudi/QuYuan1/QuYuan2/PingHu1）。

### 5. 工具链

- `probe/umd_bridge_shim.c`：`read` 日志默认关闭（`UMD_TRACE_READ=1` 才记；
  devmem 成功后 UMD 工作线程空转 `read fd3`，曾一次刷出 4.7GB/8100万行 trace，
  已删）；`0x02:0x00` 伪造；既有 `poke`（harness 未提交改动一并入库）。
- 纪律：devmem 成功后的会话进程不会自然退出（工作线程），一律 `timeout` +
  小 trace；大 trace 勿落 `reports/`（本次只记配方与结论）。

### 下一步（bA14）

1. `RGXCreateZSBuffer(hHeap, devmemctx)`（签名已扒：param_1=hHeap/param_3=devmemctx）
   → `RGXAddRenderTarget` → `RGXCreateKickSyncContextCCB(conn, devmemctx)`
   → 以 `CreateSyncPrim` 产物 + DebugPrintf/`0x652` 法整形 kickTA，进 `RGXPrepareTA`。
2. 复核 `0x02:0x07` IN 结构（对照 5.2 头）并记入 Stage B 桥表。
3. Stage B 提案不变，但 Sync 行已可写死真值（memType=`0x100000000`）。

---

## 上次会话进展（bA12：Stage A 收官评估，未提交）

接 bA11。kick 入口已探明，要求事件过滤器对象（`[renderctx+0x50]`），
我方流程未建——属 UMD 内部对象图缺失，非线格式问题。

### Stage A 结论：离线 triage 已覆盖全链路骨架

节点选择、Connect、BVNC、info 页、堆表＋名、sync 全家、双连接、
devmem、renderctx 创建、PMR/reserve/map、OOM-stats、kick 入口形状。
剩余三项（kick 包内容、Sync memType 动态、事件过滤器）必须真实 KMD
数据，伪造边际收益已尽。详见报告 §19。

### Stage B 提案（待用户拍板）

最小内核桥（新模块，不碰活会话）：`pvr` 节点＋INIT＋Connect＋
info/mmap＋堆表＋PMR 真后备＋sync 全家＋双连接；然后 UMD 跑真实数据，
Sync/kick/PSC 自然落地。详见报告 §7。

---

## 上次会话进展（bA11：SyncPrim 现状，未提交）

接 bA10。

### 1. Sync alloc 是 lazy 的，kick 时才现身

- renderctx 创建内部经 `0x7c0a4` 注册 sync 分配回调（存 `0x9f970`），
  真正的 `0x02:0x00` 桥在首次使用（kick 准备）才发——静态单链
  `memType=2` 只覆盖三处注册点之一，动态值须到 kick 才见。
- `CreateSyncPrim` 直调在 GetFeatures 链 SEGV（`[conn+0]+0xa0` 从未被写入，
  watchpoint 全程无命中；第二 Connect 调用疑为死代码）。
- kick 入口候选：`RGXKickCDM/CDM2`、`musa_KickCETQ`、`MUSACESubmit`、
  `RGXCreateKickSyncContext`；推进需命令 BO/target/sync/ZS 全套，
  且离线包无法硬件验证——只审包。

### 2. 下一步（bA12）

features 槽写入者；ZS；kick 包捕获对照 PSC；最小命令集。

---

## 上次会话进展（bA10：render ctx 成功，未提交）

接 bA9。两条并进均有斩获：

### 1. TA timeline（sync ioctl 全家）

`PVR_SYNC_IOC_RENAME` 在假 fd 上 ENOTTY → 无 timeline → 失败。
shim 对 DRM pvr 系列（`0x6440-0x6445`，桥包除外）回 0。

### 2. device connection（决定性）

- `PVRSRVConnectionCreateDevice(b7,u0,u0)` 跑出第二套完整 Connect，
  返回 device-conn；其上 devmem＋renderctx：
  **`RGXCreateRenderContext -> 0`，outptr 非零！**（120 ops，
  `reports/umd-bridge-renderctx-trace.jsonl`）。
- 通用 conn 上同调用只到 1——渲染走 device conn。
- 给 Stage B 的输入：两种连接都要实现；`CreateDevice` 线格式已有。

### 3. 下一步（bA11）

ZS buffer → Sync alloc（memType 复核）→ kick → 最小命令集。

---

## 上次会话进展（bA9：稳定性修复 + OOM-stats，未提交）

接 bA9（同一会话连续推进）。

### 1. TA timeline 是钥匙，renderctx 建成

- `PVR_SYNC_IOC_RENAME`（`0x40206441`）在假 fd 上 ENOTTY →
  无 TA timeline → 失败。shim 对 DRM pvr 系列 ioctl
  （`0x6440-0x6445`，桥包除外）回 0 后：
  **`RGXCreateRenderContext(...) -> 0`，outptr 非零！**
- 101-op 序列：`0x82:0x8`（hRenderContext=0x6000）、
  `0x1:0x4`（hOSEvent=0x7000）、第二节点 open + `INIT(2)`、
  两次 SYNC_RENAME，无 teardown。
- 给 Stage B 的输入：整套 PVR sync ioctl 必须实现；
  render 节点要能多开。

### 2. 下一步（bA11）

ZS buffer → Sync alloc（memType 复核）→ kick → 最小命令集。

---

## 上次会话进展（bA9：稳定性修复 + OOM-stats，未提交）

接 bA8（同一会话连续推进）。

### 1. 重大纠正：非确定性主要源于自家工具

shim 的 malloc/calloc/realloc/free 拦截有竞态（非原子 resolve、
tmp 回退、日志递归），污染了之前所有“三态”观察。现默认关闭
（`UMD_ALLOC_WRAP=1` 才开）——连续运行已确定性复现
（connect/devmem/renderctx-attempt 无崩溃无挂起）。
此前“堆损坏”结论降级为待复核；ASan 实锤（自有分配器、拦截被屏蔽）
不受影响，依然成立。

### 2. OOM-stats 与 PMR 伪造现状

- 新桥 `0x6:0x27 UPDATEOOMSTATS`（in8=`aea50000 1e000000`）在
  renderctx PMR 分配后、unref 前触发；PMR 句柄递增互异、
  `uiOutFlags=0x1233`、`isSystemMem=1` 仍未越过。
- 给 Stage B 的输入：OOM-stats 需实现；工具链必须确定性。

### 3. 下一步（bA10）

抓 renderctx 期 FindHeap 名与注册表；然后 Sync → kick → 最小命令集。

---

## 上次会话进展（bA8：堆名机制落定，未提交）

接 bA7（同一会话连续推进）。

### 1. 堆名是同步拷贝，无生命周期问题

- 注册表全 dump：11 entries，0-6="General"（栈残留继承），
  7="PDS Code and Data"，8-10="USC Code"（残留继承）。
  UMD 在 details→create 间隙同步拷贝，名字稳定。
- bA7 头号嫌疑排除。renderctx 期另有一次 `DevmemFindHeapByName`
  失败（名字/注册表均未知），导致 DCE 缓冲失败返回 1——
  bA9 用同款断点法抓第四次起的查找。

### 2. 下一步（bA9）

抓 renderctx 期查找名；然后 Sync → kick → 最小命令集。

---

## 上次会话进展（bA7：renderctx 堆名查找机制，未提交）

接 bA6（同一会话连续推进）。

### 1. renderctx 走到 DCE 缓冲分配并报缺堆

- `RGXCreateRenderContext(conn, params, outptr=rdx)` 最远：
  PMR 分配 → `0x6:0x27 UPDATEOOMSTATS` → unref → 返回 1（outptr 未写）。
  58-op 序列见 `reports/umd-bridge-renderctx-trace.jsonl`。
- DebugPrintf 抓因：`DevmemFindHeapByName` 失败 →
  suballocate OUT_OF_DEVICE_VM → DCE 上下文 PDS 缓冲失败。
- 按名找堆机制：`0x94a60` 遍历（count@+0x18，表@+0x20），
  `entry+0` 为名指针；devmem 期 PDS/General/USC 三查全中。
- 堆名指针追到 mmap 区（0x77…），生命周期待定为头号问题；
  同输入三态（SEGV/挂起/返回）并存，堆损坏未排除。
- 工具：`strat`、`dumpat`、`bID*+OFF`/`bID@OFF`/`bID@@OFF`、
  `*` 前缀解引用、`conn+OFF`、8 参数调用。

### 2. 下一步（bA8）

堆名生命周期确认；然后 Sync → kick → 最小命令集。

---

## 上次会话进展（bA6：renderctx 参数与堆损坏，未提交）

接 bA5（同一会话连续推进）。

### 1. renderctx 签名破解

- `RGXCreateRenderContext(conn?, params, outptr=rdx)`；
  CCB 要求 params 非空、`+0x30/+0x34` 非零、第 7 参数非空
  （缺失报 `ppsRenderContext invalid` 返回 3）。
- params`+0x10` 须为子结构指针（零则 SEGV）；取 devctx 时行为三态：
  SEGV / 单线程 mutex 自死锁 / 返回 3——非确定性指向堆损坏。
- 新桥：`0x6:0x27 PVRSRVUPDATEOOMSTATS`（某分配失败的上报）。
- 方法沉淀：DebugPrintf 断点读参、`dumpat`、`bID*`、`call` 8 参数。

### 2. 下一步（bA7）

ASan 跑 renderctx 组合抓第一次非法写；然后 Sync → kick → 最小命令集。

---

## 上次会话进展（bA5：devmem 上下文创建成功，未提交）

接 bA4（同一会话连续推进）。

### 1. USC 堆是钥匙，devmem ctx 成功

- DebugPrintf 抓取（断 `0x92c30` 读参）：失败链
  `DevmemFindHeapByName → "Failed to find USC heap"
  (INVALID_HEAP_INDEX) → Destroy → double-free`。
- `heap_names[8]="USC Code"` 后：11 堆全建、无 teardown、
  `Ext` 返回 1（成功码；0=吞错）、`o1=o2=非零 ctx`。
- 成功后新桥（零伪造即过）：`0x6:0x9` RAM 后备 PMR ×3、
  `0x6:0x15` 预留 ×3、`0x6:0x13` 设备映射 ×3；
  另 mmap×5/munmap×3。56-op 序列见 `reports/umd-bridge-devmem-trace.jsonl`。
- 方法：ASan（exe+shim 同插）定位 UMD 错误路径 double-free；
  `Ext` 返回值语义澄清（1=成功）。

### 2. 给 Stage B 的新增输入

堆名表（General/PDS/USC）；devmem 需要 named heaps；
`HEAPCREATE` 句柄互异；11 堆真实范围。

### 3. 下一步（bA6）

用已建 devmem ctx 调 `RGXCreateRenderContext`（harness 已支持 8 参数），
目标 Sync alloc 动态现身；然后 kick；回填 0x6:0x9/0x15/0x13 真值。

---

## 上次会话进展（bA4：devmem 上下文 + PSC 门，未提交）

接 bA3（同一会话连续推进）。

### 1. 会话式 harness 与 devmem 流程

- harness 支持 `connect/buf/u32/u64/call/dump`（conn 持久化）；
  `RGXCreateDeviceMemContext(conn, &o1, &o2)` 签名已确认。
- 新桥序列（53 ops，`reports/umd-bridge-devmem-trace.jsonl`）：
  CTXCREATE → HEAPCOUNT(11) → 11×(HEAPDETAILS + HEAPCREATE)
  → 11×HEAPDESTROY → CTXDESTROY → 崩溃。
- 伪造：heapcount=11；details 按 `mt_guest_plan_heaps` 逐 index 真实范围；
  ctxcreate/heapcreate 句柄互异（全零/别名会触发不同死法）。

### 2. 根因：PSC 创建失败 + UMD 错误路径 double-free

- `RGXConstructDeviceMemContext` 报 `Failed to create PSC context`
  后 teardown；`Ext` 照例吞错返回 0（教训：只信 trace/dump）。
- ASan 实锤：0x30 devctx 被 `MTSRVReleaseDeviceMemContext` 释放后，
  又被 epilogue 释放——UMD 错误路径 bug，真机正常路径不应触发。
- `RGXCreateRenderContext` 直调返回 3（INVALID_PARAMS），确认依赖 devmem。
- memType 动态复核仍 pending。

### 3. 下一步（bA5）

PSC 创建需求（堆名？usc 堆？特性开关？）；然后 Sync → render ctx → kick。

---

## 上次会话进展（bA3：Connect 走通 + info 页破译，提交 `13e6299`）

接 bA2（同一会话， respond-all 连续推进）。

### 1. 转向：UMD 经 raw syscall 做 mmap（拦截盲区）

- `catch syscall mmap` 实锤：UMD 经 libc `syscall()` 直接 mmap
 （`fd=6, len=0x10000, prot=READ, flags=SHARED, off=0x1001000`），
  绕过 `mmap@plt` 拦截——之前“无 mmap”是工具盲区。
  在 `/dev/null` 上成功拿零页，UMD 把 64KB 零当 info 页解析。
- 失败链：`0x48980 → 0x967a0(DevmemAcquireCpuVirtAddr) → 0x98420 →
  0x8f630`，在 mmap 结果检查（`0x8f890`）走 78（`ENODEV` 类失败→映射失败）。
  断点定位法：gdb python 按文件偏移批量下断点（ASLR 下每次重算）。

### 2. 修复 + info 页破译，Connect 返回 0

- shim 拦截 `syscall()` 本体（内部改走内联汇编 `S_`），UMD DRI fd 的
  mmap 一律给匿名映射并预填；其余透传。
- 内容（S：`0x92450` 比对）：`[0]=1`（设备数）；`+0x44=0xb57`
 （能力位全覆盖）；`+0x48=0x688a847`（KMD 构建魔数，忽略 bit16）。
- 成功序列 17 ops：`INIT → Connect → event → infopage → import → mmap →
  HWPERF(0x86:0x4) → import(hPMR=0) → mmap → GETMULTICOREINFO(0x1:0xc) →
  version 重扫 → ALIGNMENTCHECK(0x1:0xa) → 0`。conn 非空。
- 给 Stage B 的硬输入：KMD 需 DRM mmap-offset 分配（info PMR 在
  `0x1001000`）；version 名 `pvr`；Connect 上报 allow-list Bvnc 之一。

详见 `reports/bridge-stage-a-triage.md` §9（trace 已更新为成功序列）。

### 4. 下一步（bA4）

同一进程多步调用（conn 持久化）→ devmem ctx → render ctx；
`RGXCreateRenderContextCCB` 参数形状已初探（见报告 §9 末尾）。

---

## 上次会话进展（bA2：BVNC 门定位 + core allow-list，提交 `b4ddfcd`）

接 bA1 继续（同一会话）。

### 1. 返回码解码

78 = `MTGPU_ERROR_DEVICEMEM_MAP_FAILED`（枚举数得，非猜测）。

### 2. 系统二分（均为 T）

Connect OUT 全零→全非零→单字段非零、事件句柄 0→0x2000，
中止序列完全不变 → Connect OUT 不是当前 blocker 的充分条件。
事件句柄非零使清理多走 release-event 一步（句柄被真实消费的又一证据）。

### 3. gdb 抓栈定位失败分支

- `UMD_TRAP="1:1"` 在 disconnect 前 SIGTRAP，抓到清理栈；
  上层在 `cmp rdx,0x23 / cmp edx,0x660` 门走失败分支，`r14=78` 预置返回。
- 门逻辑还原（S，`srv_um.dis` 文件偏移）：
  连接对象 u64 先与 `0x0001000000000000`（`0x3b8c5`）、
  `0x0023000406600017`（`0x3b8d8`）精确比对——UMD 写死的 core allow-list，
  **未来 KMD 必须原样上报其中之一**；未命中则强制覆写为后者再过
  `top16==0x23`、`bits[31:16]==0x660`、`C==0x17`、`V==4` 四道子门。
- 即使伪造精确值仍走失败分支：被检槽 `[rbp-0x70]` 经 `0x92550` 写入，
  数据流待 bA3 精读（对照第二次 Connect `0x927b4` 的 `[r15]` 槽）。
- 接受路之后还有 `InitMTFeatures(0x51b20)` / `GetFeatures(0x518e0)`；
  AppHint 名/默认在 rodata 可静态枚举（地址已记入报告 §8）。

详见 `reports/bridge-stage-a-triage.md` §8（`reports/umd-bridge-connect-trace.jsonl`
已更新为 13-op canonical trace）。

### 4. 下一步（bA3）

精读 `0x92550` 的 Bvnc 槽写入者；跟接受路进 `InitMTFeatures`；
然后依次点亮 heap → Sync → RGX 上下文 → kick。

---

## 上次会话进展（bA1：桥接 Stage A 离线 triage，提交 `c98c197`）

用户决定：按 PVRSRV 桥接推进。先做阶段 A（离线 triage），第一步已落地。

### 1. 工具：UMD 桥接 tracer（新增入库，未提交）

- `probe/umd_bridge_shim.c`（LD_PRELOAD）：拦截 `/dev/dri/*` open、
  `0xc0206440` 桥包 / `0x40046445` INIT ioctl（输出清零 + 按表回填），
  记录 mmap/read/pread/lseek；只读零写，不碰硬件。
- `probe/umd_connect_harness.c`：dlopen + dlsym 驱动 UMD 导出函数。
- 结论全部可复现：`UMD_TRACE=... LD_PRELOAD=... ./build/probe/umd_connect_harness <lib> PVRSRVConnect 0`。

### 2. 材料恢复（重启后 /tmp 被清空）

- `build/legacy-umd-pvr-connect-candidate/rootfs/` 的 UMD 经 sha 确认无损，
  已复制回 `/tmp/mtt-linux-umd-5.2.0/root/`；审计脚本重跑 205/205 零 diff。
- 教训：`/tmp` 易失，关键结论必须落盘 `reports/`（本次已做）。

### 3. 关键发现

- **节点选择**：UMD 扫 render 节点，比 version 名**全等 `pvr`** 才停
  （`mtgpu` 会连扫 256 个节点；另有 `PVRDRMGetRenderFromFD` 比 `mtgpu`，
  Connect 走 `pvr` 这条）。未来自研 DRM 节点给 UMD 用的 version 名取 `pvr`。
- **Connect 序列**（伪造下走 7 步后主动断开，句柄链自洽）：
  `INIT(1) → Connect(0x1:0x0, in=80000850/0/10000/20) → event → infopage(hPMR=0x1000)
  → import(align/size=0x1000,hPMR=0x1001) → unref → release → disconnect`，返回 78。
  info 页未被读取（无 mmap/pread）；最可能是 Connect OUT 17B
 （Bvnc/caps/arch）全零未过检查——下一步伪造它。
- **Sync memType**：全库唯一 `(0x02:0x00)` 调用链顶端传 `edx=0x2`，
  证据指向恒为 2；待走通后动态复核。5.2 KMD 源码里该字段只出现在声明中。
- 26 缺失 ID 分组已列（SYNC 事件组/MM 扩展/RGXCMP/RGXTA3D 高编号/PFM 等），
  Connect 序列未命中任何一个。

详见 `reports/bridge-stage-a-triage.md` + `reports/umd-bridge-connect-trace.jsonl`。

### 4. 下一步（bA2）

伪造 Connect OUT（Bvnc+caps+arch）越过 disconnect；Bvnc 真值候选为 UMD 内
expected 常量或硬件只读寄存器（另起只读 helper）。

---

## 上次会话进展（r44：r42 真机验证完成 + 卸载 WARN 新发现）

用户指令：重启后在本机继续测试。

### 0. 重启后环境

- `sudo` 可用（`NoNewPrivs=0`）；`mt_guest_probe` 未加载，旧会话已随重启消失，
  无需销毁任何东西。
- `00:0e.0` 被官方 `mtgpu` 绑定，但它启动时即 `PhysHeapsInit` 失败（`-19`，
  无设备节点，引用 0）；显示走 QXL。已解绑（可逆），官方驱动模块仍在内存中。

### 1. 会话重建（未绕过任何门禁）

- unbound 后 BAR 读数 `Guest=2/FW=1`（宿主侧残留，30 秒 3 次轮询稳定），
  `fresh-trial.py` 预检判不合格，主模块 probe 自身也要求 `0x890==0`——均未绕过。
- 按 r36/r28 定式：`mt_cold_disconnect` 只读 dry-run
  （6 DM 全环 idle、`started=0`、`FW=1`；`fw_pa=0x779fef000` 为宿主重启后新值）
  → `finish=1` 写 Guest OFF → 双重确认 `Guest=0/FW=1` → 卸载 helper。
- `fresh-trial.py --run --runtime-context`：`connected=1`、`published=1`、
  `guest=2 firmware=2 started=1 pinned=1`。主模块为新布局重编，
  证据 `build/fresh-trials/20260930T052647Z-28c45d79/`（gitignored）。

### 2. r42 真机验证通过

- `mt_live_3d_drm` 加载不再 `-EBUSY`：
  `2D VM (pages=18, maps=20/15872) & 3D VM (pages=21, maps=23/15872)`。
- `mt-3d-check 20`：`2D=20 3D=23/15872`（上限 15872 真机实测 = r42 推导值），
  20 帧 100% + render-target 绑定/VRAM 读回全部 OK。
- 详见 `reports/r44-r42-live-verification.md`。

### 3. 新发现（预先存在，与 r42 无关）：sealed 恢复空间卸载报 WARN

- `rmmod mt_live_3d_drm` 两次均在 `release_unpublished` 留两条 WARNING
  （674/679 行 `destroy(space_3d/2d)` 返回 `-EBUSY`）。
- 根因：两空间都已 `seal`，而 `mt_gpu_vm_fini` 按设计拒绝已 seal 的 VM
  （r42 前后相同；r41 从未 seal 后卸载，故无人见过）。
- 泄漏：2 个 `mt_vm_vram` 对象 + 所绑 BO；每周期主模块引用计数 +26
  （VRAM backing 的 `__module_get` 永不配对，`store->objects` 不回落）。
- 已验证会话在 WARN 卸载后依然健康（第二次加载 + 3 帧 `seq=22..25` + 读回全过；
  第二次加载节点顺延为 `renderD129`/`card2`，`mt-3d-check` 硬编码路径，
  用临时 symlink 覆盖，用完即删）。
- 规则：已 seal 的恢复模块不要热卸载（与 r32/r34 一致）；WARN 保留不静默；
  真正的释放需要 context-withdrawal/TLB 协议（独立工作项，不动手）。
- 当前终态：新布局主模块 + 活会话（`guest=2 firmware=2 started=1`，
  `pending=0 completed=25`），恢复模块已卸载，`00:0e.0` 归自研驱动；
  taint 新增 `W` 位。

---

## 上次会话进展（r43：r42 遗留 errno 等价性收尾，提交 `c104a7d`）

用户决定：先收尾 r42 遗留；r42 真机验证直接在本机尝试 root。

### 1. 本机提权不可行（已实测）

`NoNewPrivs=1`、`CapEff=0`：`sudo -n true` / `sudo true` 均报
"no new privileges" 失败，`su` 认证失败。
**本容器内任何重载主模块 / insmod / dmesg 操作都不可能执行。**
r42 真机验证（512-mapping 真机场景、QUERY `vm3d_max_mappings` 真机值）
只能到有 root 权限的终端上做。

### 2. r42 遗留第 3 项关闭：重叠检测前移的 errno 等价性

对照 `ee1dd8d^` 旧实现逐项核对，新旧差异只有两处优先级边：

- `va + bytes` 上溢：旧经 planner 报 `-EINVAL`，新预检报 `-ERANGE`。
  **r42 文档「调用者观察到的 errno 不变」对此边不成立**，已在
  `reports/r42-vm-mapping-scale.md` 第 7 节勘误。
- 重叠且同时超 mapped-byte 预算：旧 planner 内顺序 E2BIG 先，
  新预检 `-EEXIST` 先。单 bind 的 `-EEXIST` vs `-ENOSPC` 顺序新旧一致。
- plan 阶段拒绝（`-E2BIG` / 页预算 `-ENOSPC`）发生在 `grow()` 之后，
  可能保留已增长的空暂存数组；image / count / 页数 / 引用计数精确不变
  （良性过分配；旧固定数组无此现象）。
- 全部 bind 调用方（`mt_vm_vram_bind`、`mt_gem_bind_handle`、
  `mt_process_resources_bind(_pools)`、`mt_boot_bo_bind`）均透传 errno，
  无优先级分支；用户态只断言计数值。故两处变化无实质影响。

测试：`tests/gpu_vm_scale_test.c` 新增 `errno_precedence()`
（2×32 MiB BO 顶满 64 MiB 预算 + 直接调 planner 交叉验证，ASan + UBSan），
全量 21 测试通过，三处 `W=1` 零警告，ABI 门禁通过。
`scripts/verify-runtime-integration.py` 无需改注册（同一二进制）。

### 3. r42 真机验证仍未完成（原因：见第 1 条，需用户异地执行）

---

## 上次会话进展（r42，提交 `ee1dd8d`）

### 1. 订正了 r41 的一处错误结论

r41 报告与 `PROTOCOL-NOTES.md` 把「S3000 vGPU MMU 单个虚拟地址空间最多容纳
24 个 mapping ranges」记为**硬件物理上限**。核查确认这是**驱动自己的编译期常量**：

- `MT_BOOT_MAX_RANGES 24U` 唯一实际用途是 `struct mt_gpu_vm` 中
  `bindings[24]` 内联数组的长度，以及 `mt_gpu_vm_plan()` 里两个同长的**内核栈数组**；
- 真实 MMU 是三级页表 walk（`mt_mmu.h`，逆向自 `mtkm64.sys`）：
  PC 1024×4B（每项 1 GiB）→ PD 512×8B（每项 2 MiB）→ PT 512×8B（每项 4 KiB）；
- **硬件对映射数量没有任何上限**。单个地址空间的实际上限来自页表页预算，
  即 `MT_BOOT_MAX_MAPPED_BYTES`（64 MiB）对应的 16384 个页表项；
- 任何真实图形栈（需要成百上千个 buffer 映射）都过不了 24 这一关，
  所以这是前置阻塞项。

### 2. 上限改为从页表几何推导

`mt_mmu_bootstrap.h` 新增 `mt_boot_max_ranges(table_pages)`：
`min((table_pages - 1) × 512, MT_BOOT_MAX_MAPPED_BYTES / 4096)`，root 页不可用于映射。

- 生产配置（32 页）实测上限 **15872**，而非 24；
- `mt_mmu_build_pages()` 改为按调用方实际页预算校验，
  页预算耗尽仍由既有循环返回 `-ENOSPC`（那才是贴近硬件的真实边界）。

### 3. 映射表与规划暂存移出内联数组和内核栈

- `struct mt_gpu_vm.bindings[24]` → 按需增长的堆指针；
- 新增 `ranges` / `page_lists` 两个暂存数组，同样堆分配。
  **必须这样做**：直接把 24 改成 15872 内联会让结构膨胀到约 500 KB，
  且每次 `mt_gpu_vm_bind_many()` 在 16 KB 内核栈上压入两个大数组，直接爆栈；
- `mt_gpu_vm_grow()` 按 2 的幂增长，稳态不重复分配；分配失败保留旧数组与旧
  `count`，被拒绝的 bind 不污染既有 plan；
- `mt_work_job.bos[]` 同样是按映射数定长的内联数组，一并改为精确分配的指针。

### 4. 事务性语义与 errno 保持不变

- `bind_many()` 改为**先整批校验、再 grow、最后原地暂存到尾部**；
  重叠检测从 planner 内部提升到校验循环，故冲突请求既不增长数组，
  也不改变 image / count / 页数 / 任何引用计数；
- 范围预检顺序与 planner 自身一致（`va >= 1<<40` → `-EINVAL`，
  `bytes > (1<<40)-va` → `-ERANGE`），调用者观察到的 errno 不变；
- `mt_gpu_vm_commit()` 改为只清空退役尾部（原整体 memset+memcpy 与原地暂存矛盾）；
- 修正陈旧指针隐患：`plan()` 中 `page_lists[i]` 原先仅在 BO 有 `page_pa` 时写入，
  否则沿用上次 plan 的值——改为复用暂存数组后这会成为真实映射错误来源，
  现改为无条件赋值。

### 5. 补全共享 ABI 校验（原本正是它放过了本次变更）

`scripts/verify-runtime-integration.py` 原先只比对 `mt_guest` 一个结构。
`mt_guest_device` 内嵌 `address_spaces`，所有 recovery 模块都直接读主模块分配的
`mt_gpu_vm` / `mt_vm_vram` 字段——本次 `mt_gpu_vm` 布局变更正是因此被静默放过。

现对 7 个共享结构（`mt_guest`、`mt_guest_device`、`mt_gpu_vm`、`mt_vm_binding`、
`mt_vm_vram`、`mt_vm_store`、`mt_work_job`）取 pahole 摘要、
写入 `reports/shared-abi-baseline.json` 并在后续构建上门禁。

### 6. UAPI

`struct drm_mt_query` 尾部追加三个只读字段（56 → 80 字节，既有偏移不变）：

```c
__u64 vm2d_mappings, vm3d_mappings;
__u64 vm3d_max_mappings;
```

目的：让用户态可直接看到地址空间余量而不必猜测。
`mt-3d-check` 增加断言 `vm3d_max_mappings > 24`。

### 7. 验证结果

- 新增 `tests/gpu_vm_scale_test.c`（ASan + UBSan）：
  - 推导上限（root 页不可映射、2 页 = 512 项、页几何 vs mapped-byte 取小、
    `capacity < 8192` 时为 0 并被 `init` 拒绝、超上限 capacity 收敛）；
  - **512 个同时存在的映射**（旧上限 21 倍），共占 6 个页表页，
    对全部 512×4 个页面做**独立三级 walk 逐页核对**物理地址；
  - 拒绝路径不变式：重叠 / 非对齐 / 越界 / 非法 flags / 外部 store /
    页预算耗尽，逐项确认 image、count、页数、**数组容量**、引用计数全部未变；
- 全量 21 个测试通过；`kernel/`、`kernel/recovery/`、`kernel/selftest/`
  三处 `W=1` 零警告零错误（`mt_drain_pending.c` 与 `mt_fw_event_io.h`
  的 3 条警告是既有的，未改动时同样存在）；
- **ASan 抓到一处真实泄漏**：`mt_work_job_prepare()` 在 `mt_bo_gpu_begin()`
  失败回滚时只释放了 BO pin，漏掉了新分配的 pin 数组。已修。

### 8. 真机验证未完成（原因明确，需用户决定）

当前 `mt_guest_probe` 仍在运行，且是**用旧 `mt_gpu_vm` 布局编译的**
（内联 `bindings[24]`，`sizeof` 与字段偏移均不同）。recovery 模块按新布局
访问主模块分配的对象，`space_2d->vm.max_ranges` 读到 0，
`create()` 在 `mt_gpu_vm_init()` 处失败，insmod 报 `-EBUSY`。

**要完成真机验证必须重新加载主模块，这会销毁已建立的固件会话**
（Guest=2 / FW=2 / started=1），属影响硬件状态的操作，未擅自执行。

---

## 关键阻塞：为什么不是三角形光栅化

核查 11 个 3D 上下文 BO 的实际内容（`kernel/mt_gfx_context_data.h`）：

| BO | 字节 | 非零 | 内容 |
| --- | --- | --- | --- |
| 0 | 3072 | 227 | PDS 上下文切换程序 |
| 1 | 6144 | 1216 | USC shader |
| 2 | 776 | **0** | DCE context switch snapshot |
| 3 | 468 | **0** | **TA state** |
| 4, 5 | 1024 | **0** | PDS |
| 6, 7 | 16400 | **0** | **VDM uniform PDS state** |
| 8, 9 | 16400 | **0** | **DDM uniform PDS state** |
| 10 | 8192 | **0** | **Rasterisation context state** |

**TA state、光栅化上下文、以及全部 VDM/DDM 程序全为零。**
当前 3D 上下文只有「上下文切换」能力，没有「绘制」能力。
硬件能消费包并返回 `result=0`，**不等于发生了光栅化**。

原因：`FUN_00183d30` 的原厂错误字符串即
`RGXGenerateContextSwitchUniformTasks: Failed to create USC task`。
它只负责生成 uniform 存取程序，且因上下文描述符全零而直接失败。
真正的顶点取数程序由原厂 **PSC 编译器闭包**（`001a4fc0`..`001b6e40`）现场生成，
不是可以手写常量填入的模板。

**r41 文档写的下一步「顶点缓冲 + 单三角形光栅化」被这条硬依赖挡住。**
可达路径只有两条：

1. 逆向重实现 PSC 编译器以合成顶点取数程序；
2. 实现 PVRSRV ioctl 桥接，让原厂 Linux UMD 的渲染路径跑在自研驱动上。

**建议路径 2**——这是通往可用图形栈（GL/Vulkan/合成器）的唯一正确道路，
但工作量远大于 r42，需要单独做阶段设计。

---

## 辅助工具

- `scripts/trace-packet-field-map.py`：对原厂 Linux UMD 逐 word 差分探测
  （121 个可达 word），把 5 个描述符字段映射到包内偏移，
  落盘 `reports/packet-field-map.json`。
  这是后续定位顶点/图元寄存器字段的工具基础，
  但它给出的是**数据依赖，不解释寄存器语义**，不要当作解码结果使用。
- `scripts/verify-runtime-integration.py`：全量构建 + 21 个 RAM 测试 + ABI 门禁。
  运行方式：`cd mt-vgpu-guest && python3 scripts/verify-runtime-integration.py`

---

## 常用命令

```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest

# 全量离线验证（21 测试 + W=1 构建 + ABI 门禁）
python3 scripts/verify-runtime-integration.py

# 内核构建
make -C kernel W=1 && make -C kernel/recovery W=1 && make -C kernel/selftest W=1

# 用户态工具
make -C userspace

# 真机状态（需要主模块已按新布局重载）
sudo insmod kernel/recovery/mt_live_3d_drm.ko
sudo ./build/userspace/mt-3d-check 5
sudo rmmod mt_live_3d_drm
```

注意：r42 之后，真机命令在**主模块按新布局重新加载之前**会失败（`-EBUSY`）。

---

## 下一步（待用户决定）

1. ~~r42 真机验证~~ 已由 r44 完成（20 帧 + 读回 + 上限实测）。
2. **选定三角形/渲染路径**：逆向 PSC 编译器，还是走 PVRSRV ioctl 桥接
   让原厂 UMD 跑渲染路径（建议后者）。
3. 若走 UMD 路径，需先做阶段设计：梳理原厂 UMD 对内核侧
   `PVRSRV` ioctl 的完整需求面。
4. ~~r42 遗留第 3 项（重叠检测 errno 等价性）~~ 已由 r43 关闭。
5. 远期：sealed VM 释放需要 context-withdrawal/TLB 协议；
   在此之前已 seal 的恢复模块不要热卸载（每次卸载漏 2 VM 对象 + 约 26 主模块引用）。

---

## 已完成阶段索引

| 阶段 | 内容 |
| --- | --- |
| r22b | V2 / OSID 6 协商、显存堆重建、MMU 根上下文、页表映射 |
| r23–r31 | TQX 2D 通路、LMA 读写、firmware 连接、显存保留池 |
| r32–r34 | 独立 DRM 节点、GEM 显存管理、原生 GPU 填充与大表面 |
| r35–r38 | 3D 寄存器与 18112 字节任务包、CSW 状态字、DM2 Universal 队列闭环 |
| r39 | 多帧批量压测、Ring 回绕验证（106 帧 100% 成功） |
| r40 | 用户态 DRM 接口、2D/3D 双 VM 空间隔离 |
| r41 | 3D Render Target 动态绑定与 VRAM 读回（真机 20 帧成功） |
| **r42** | **GPU VA 映射可扩展化，移除 24 mappings 假上限（本次）** |

---

## 注意事项

- `mt_guest_probe` 仍在运行，固件会话已建立，本次**未触碰**；
  内核 taint 为 12800（外部/未签名模块），与进入本会话时一致。
- 用户已加入 `video` 与 `render` 组，权限不是问题。
- 实验副本与临时模块不得在移除入口保护后直接试载。
- 未验证事项见 `reports/r42-vm-mapping-scale.md` 第 6–7 节
  （r43 已离线关闭第 3 项；真机两项已由 r44 完成，
  见 `reports/r44-r42-live-verification.md`）。
- 已 seal 的恢复模块（`mt_live_3d_drm` 等）不要热卸载：
  `destroy` 必报 `-EBUSY` WARN 并泄漏（r44 第 3–4 节）。
