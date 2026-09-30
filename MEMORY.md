# MEMORY — 摩尔线程 vGPU 驱动适配

最后更新：2026-09-30（bA7：renderctx 堆名查找与生命周期问题；未提交）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 目标

在 Debian 13.7 / Linux 6.12.107 虚拟机中，为摩尔线程 S3000 vGPU
（PCI `1ed5:0222`，subsystem `1ed5:1101`，Guest 设备 `0000:00:0e.0`）
自研 Linux 原生驱动，最终实现可用的图形加速（GL/Vulkan/合成器）。

当前显示仍为 QXL + llvmpipe 软件渲染。GPU 硬件通路已打通并可执行任务，
但距离可用图形栈还差关键环节（见「关键阻塞」）。

---

## 本次会话进展（bA7：renderctx 堆名查找机制，未提交）

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
