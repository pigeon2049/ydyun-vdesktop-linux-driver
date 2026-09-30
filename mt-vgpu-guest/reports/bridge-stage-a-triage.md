# Bridge Stage A (bA1)：离线 UMD 桥接 triage——tracer、节点选择与 Connect 序列

日期：2026-09-30。全部离线：无 PCI/BAR 写、无模块加载、无 GPU 提交。
方法：`LD_PRELOAD` 拦截 + 反汇编交叉验证（报告内每条结论标注来源 T=动态 trace / S=静态反汇编 / H=头文件）。

## 1. 工具（新增入库）

- `probe/umd_bridge_shim.c`：拦截 `/dev/dri/*` open（给 `/dev/null` dup fd）、
  两条 PVR ioctl（`0xc0206440` 桥包、`0x40046445` INIT），输出清零伪造、
  按表(`canned[]`)回填关键输出；`mmap/read/pread/lseek` 只记录。
  JSONL 落盘 `$UMD_TRACE`。
- `probe/umd_connect_harness.c`：`dlopen` + `dlsym` 调用 UMD 导出函数。
- `probe/Makefile`：新增两个目标；`mt-status` 不变。

## 2. 材料恢复（重启后 /tmp 被清空）

- `build/legacy-umd-pvr-connect-candidate/rootfs/`（gitignored，但在盘上）
  的 `libsrv_um_MUSA.so.1.0.0` sha256 与审计期望完全一致
  (`b3058c02…`)——身份无损，已复制回 `/tmp/mtt-linux-umd-5.2.0/root/` 供脚本使用。
- `scripts/audit-legacy-umd-pvr-bridges.py` 重跑：205/205，26 缺失，零 diff，可复现。
- 教训：`/tmp` 下的反汇编（58 MB）、DKMS 解包、trace 均为易失；
  关键结论必须落盘到 `reports/`（本文件 + trace JSON），源文件在库内。

## 3. 节点选择（T+S）：version 名必须是 `pvr`

- UMD 扫描 `/dev/dri/renderD128…`（T：先 render 节点，不先 card），每节点发
  `DRM_IOCTL_VERSION`（`0xc0406400`），随后 `strcmp(name, "pvr")`（S：`0xa4989`）。
  只有名字全等 `pvr` 才停（T：改 shim 返回 `pvr` 后首节点即中，之前 `mtgpu` 连扫 256 节点）。
- 另一函数 `PVRDRMGetRenderFromFD` 比的是 `mtgpu`（S：`0xa4e57`）——两条路径并存；
  Connect 走的是 `pvr` 这条。**未来自研 DRM 节点给 UMD 用的 version 名取 `pvr`**
  （现有 `mt-3d-check` 按路径打开，不受影响）。
- 副发现：shim 初版 `date/desc` 置空导致 UMD 内 `strdup(NULL)` 崩溃（T+gdb），
  按真实语义回 `""` 后解决——真正的 KMD 也必须返回合法三字符串。

## 4. Connect 初始化序列（T，伪造输出下走到第 7 步后主动断开）

```text
INIT(init_module=1)
0x1:0x0 Connect            in16  80000850 00000000 00010000 00000020
0x1:0x2 ACQUIREGLOBALEVENTOBJECT  in0/out12
0x1:0xf ACQUIREINFOPAGE    in0/out12 -> hPMR=0x1000（伪造）
0x6:0x6 MM:PMRLOCALIMPORTPMR in8(hPMR=0x1000) -> align/size=0x1000,hPMR=0x1001（伪造）
0x6:0x7 MM:PMRUNREFPMR      in8(hPMR=0x1001)
0x1:0x10 RELEASEINFOPAGE   in8(hPMR=0x1000)
0x1:0x1 DISCONNECT
PVRSRVConnect -> 78
```

- 句柄链 verified（T）：伪造的 `0x1000/0x1001` 原样出现在后续输入里——
  UMD 确实消费这些输出，伪造必须自洽。
- Connect 输入解码（H，对 `common_srvcore_bridge.h`）：
  `build_options=0x80000850`、`DDKbuild=0`、`DDKversion=0x10000`、`flags=0x20`，
  与 `verify-legacy-umd-pvr-connect.py` 的旧断言一致。
- 中止点：release 后立刻 disconnect。info 页在此路径下**未被读取**
  （T：无 mmap/pread/lseek/mmap 命中；read 仅 1 次读 fd 3 配置文件）。
  最可能原因：Connect OUT（17B：`ui64PackedBvnc/u32 eError/u32 caps/u8 arch`，H）
  全零——Bvnc/caps 未过 UMD 检查。**下一步：伪造 Connect OUT 后看序列是否越过 disconnect。**

## 5. Sync `ui64MemType` 实值（S，单调用链，待动态复核）

- 全库仅一处 `(0x02:0x00)` wrapper（`0x390d0`，in8/out32；输出布局与 5.2 头
  `8/32` 逐字节吻合：handle@0、PMR@8、eError@16、BlockSize@20、VAddr@24）。
- 8 字节输入来自调用方 `rsi`；直接调用者 `0x9f970` 经 `rdx` 透传；
  `0x9f970` 地址被取用后存入回调表，由 `0xa00a1` 处以 **`edx=0x2`** 发起。
  证据指向 **memType恒为 2**；待 Connect 走通后由动态 trace 一锤定音
  （届时 allen 值、是否分场景变化一次看清）。
- 5.2 Host KMD 源码中 `ui64MemType` 仅出现在声明里，
  实现（预编译 `objs/`）不可见——忽略 memType 的 2.3 handler 行为
  只能由 UMD 实测反推，这正是 tracer 存在的意义。

## 6. 26 个缺失 ID 的分组（H，对 JSON；渲染主路径相关度初判）

缺失：`0x02:0x0a-0x0e`（SYNC 事件/checkpoint 组）、`0x06:0x2a/0x30/0x31`
（MM 扩展）、`0x81:0x0d-0x10`（RGXCMP）、`0x82:0x11-0x14`（RGXTA3D 高编号）、
`0x86:0x0f-0x11`（HWPerf/PFM）、`0x88:0x04-0x07`（RGXKICKSYNC?/TQ）、
`0x89:0x08-0x0a`（RGXTIMERQUERY/TDM）。
Connect 序列至今未命中其中任何一个；`RGXKICKTA3D3(0x82:0x0e)` 在覆盖侧。
结论待动态序列走到 kick 后再定；当前没有证据表明主路径被缺失 ID 挡住。

## 8. bA2：Connect 中止根因追到 BVNC 门（仍未越过， frontier 精确定位）

- 返回码解码（H）：78 = `MTGPU_ERROR_DEVICEMEM_MAP_FAILED`
 （`mtgpu_errors.h` 枚举，`MTGPU_OK=0` 起第 79 项）。
- 系统二分（T）：Connect OUT 全零→全非零→单字段非零，中止序列**完全不变**；
  故 Connect OUT 不是当前 blocker 的充分条件（但仍可能是必要条件）。
- `UMD_TRAP="1:1"` + gdb 在 disconnect 前抓栈（T）：
  清理函数 → `PVRSRVConnect` 上层在 `cmp rdx,0x23 / cmp edx,0x660`
  门处走失败分支（`je` 成功路未中），`r14=78` 预置为返回值。
- 门逻辑还原（S，`srv_um.dis` 文件偏移）：
  - `0x3b8bb` 取连接对象 u64 → 先后与
    **`0x0001000000000000`**（`0x3b8c5`）、**`0x0023000406600017`**
    （`0x3b8d8`）精确比对，命中任一即走接受路（`0x3ba48`）——
    这是 UMD 为本代硬件写死的 **core allow-list**，未来 KMD 必须原样上报其中之一。
  - 未命中则强制覆写为 `0x0023000406600017` 再过三道子门：
    `top16==0x23`、`bits[31:16]==0x660`、`C==0x17` 且 `V==strtol("4")==4`
    （`0x3b9c8/0x3b9d3/0x3ba50/0x3ba5d`）。
  - 即使用精确值 `0x0023000406600017` 伪造，仍走失败分支——
    被检值可能不是 Connect OUT 的直接拷贝（`[rbp-0x70]` 经 `0x92550`
    连接创建函数写入，数据流待 bA3 精读），或接受路之后还有检查。
- 排除项（T）：info 页无 mmap/pread/lseek（shim 已覆盖），无其他 open
  （shim 现记录全部 open，本次零新增），无 passthrough ioctl。
- bA3 切入点（全部有文件行号）：
  1. 精读 `0x92550`（`0x92550-0x92858`）：`[rbp-0x70]` 到底写入什么；
     对照第二次 Connect 调用（`0x927b4`）的 Bvnc 存储槽（`[r15]`）。
  2. 接受路 `0x3ba48 → 0x3b8eb` 之后：`InitMTFeatures(0x51b20)` /
     `GetFeatures(0x518e0)` 的输入来源（`r12+0x80/0x88` AppHint？）。
  3. AppHint 名/默认值在 rodata（`0x40a433/0x40a444/0x40a465/0x40a489/0x40a49c`），
     可静态枚举。
- 附带产出：shim 新增 `UMD_TRAP="bridge:func"`（SIGTRAP 抓栈）、全部 open 记录、
  `canned[]` 伪造表（含事件句柄 0x2000 使清理路径多走 release-event 一步，
  证明伪造值被真实消费）。

## 9. bA3：Connect 走通（返回 0）——info 页 mmap 与内容伪造是钥匙

- 关键转向（T+gdb）：`catch syscall mmap` 证明 UMD 经 libc `syscall()`
  直接发 mmap（`fd=6, len=0x10000, prot=READ, flags=SHARED, off=0x1001000`），
  绕过 `mmap@plt` 拦截——之前“无 mmap”结论是拦截盲区，不是事实。
  在 `/dev/null` 上该 mmap 成功返回零页，UMD 把 64KB 零当 info 页解析。
- 失败链还原（S，`srv_um.dis`）：`0x48980 → 0x967a0(DevmemAcquireCpuVirtAddr)
  → 0x98420 → 0x8f630`，在 `0x8f890` 因 mmap 结果检查走 78 分支。
  疑似有两处 mmap（trace 见两次 `mmap_fabricated`，同 offset）。
- 修复：shim 拦截 `syscall()` 本体（内部改走内联汇编 `S_`），
  凡 UMD DRI fd 的 mmap 一律给匿名映射并预填 info 页；其余原样透传。
- info 页内容破译（S：`0x92450` 起的比对）：
  - `+0x44` u32：必须覆盖 `0xb57` 各位（`~x & 0xb57 == 0`），取 `0xb57`；
  - `+0x48` u32：必须 `^ 0x688a847` 后忽略 bit16 为零，取 `0x688a847`；
  - `+0x0` u32 = 1（设备数，沿用 v1 假设，验证通过）。
  填入后 `PVRSRVConnect(0) -> 0, conn != NULL`，17-op 成功序列：
  `INIT → Connect → event → infopage → import → mmap → HWPERF(0x86:0x4,
  RGXACQUIREHWPERFFSETTINGS) → import(hPMR=0) → mmap →
  GETMULTICOREINFO(0x1:0xc) → version 重扫 → ALIGNMENTCHECK(0x1:0xa,
  in=用户指针+0x27) → 返回 0`。后三者输出全零即过（暂）。
- 附带产出：`out_written` 日志（伪造落盘可审计）、`S_` 内联汇编、
  UMD fd 追踪表。`reports/umd-bridge-connect-trace.jsonl` 已更新为成功序列。
- 给 Stage B 的输入（已定）：KMD 必须实现 DRM mmap offset 分配
  （`drm_vma_offset_manager` 类机制），info PMR 固定落在 `0x1001000`；
  version 名 `pvr`；Connect 行为（Bvnc 上报其一 allow-list 值）。

## 10. bA4：devmem 上下文流程打通到堆创建，卡在 PSC 创建（双重释放为 UMD 错误路径 bug）

- 会话式 harness（`connect/buf/u32/u64/call/dump`，conn 进程内持久）：
  `PVRSRVCreateDeviceMemContextExt(conn, &o1, &o2)`（签名已由反汇编确认）。
- 新桥（T）：`0x6:0xf DEVMEMINTCTXCREATE` → `0x6:0x1e HEAPCOUNT(=11)`
  → 11×(`0x6:0x20 HEAPDETAILS` + `0x6:0x11 HEAPCREATE`)
  → 11×`0x6:0x12 HEAPDESTROY` → `0x6:0x10 CTXDESTROY` → 崩溃。
  53-op 完整序列见 `reports/umd-bridge-devmem-trace.jsonl`。
- 伪造（H 对 2.3 头 + 我方堆表）：heapcount=11；details 按
  `mt_guest_plan_heaps` 逐 index 回真实 base/length（4K 页）；
  ctxcreate 回非零句柄 + 64B cacheline；heapcreate 按 base 回不同句柄。
- 根因链（S+gdb+ASan）：
  - `RGXConstructDeviceMemContext` 在堆建完后报
    `Failed to create PSC context`（S：`0x70373`），走 teardown
    （11 destroys + ctxdestroy 均在 trace 中），
    随后 `Ext` 照例吞错返回 0（教训：返回值不可信，看 trace/dump）。
  - teardown 中 ASan 实锤真 double-free：0x30 devctx 先由
    `MTSRVReleaseDeviceMemContext`（`0x6fb8c→0x41a10`）释放，
    又被 epilogue（`0x6fafe→0x8e200`）释放——UMD 错误路径 bug，
    真机上此路径不应被触发。
  - `PVRSRVCreateRenderContext` 直调（无 devmem ctx）返回 3
    （INVALID_PARAMS），确认 render ctx 依赖 devmem ctx——PSC 门是必经项。
- memType 动态复核仍 pending（sync alloc 在 render ctx 之后）。
- 给 Stage B 的输入（新增）：堆表 11 项真实范围（shim 内 `heap_ranges[]`
  即 KMD 将来要上报的值）；`DEVMEMINTHEAPCREATE` OUT 句柄必须互异。

## 11. bA5：devmem 上下文创建成功（USC 堆命名是钥匙）

- DebugPrintf 抓取（gdb 断 `0x92c30` 读参）：失败链为
  `DevmemFindHeapByName → "Failed to find USC heap"
  (INVALID_HEAP_INDEX) → Destroy → double-free`。
  PDS 命名（heap 7）一次即中；缺的是 **USC**。
- `heap_names[8] = "USC Code"` 后：11 堆全建、无 teardown、
  `Ext` 返回 1（成功码！之前返回 0 恰是吞错路径），
  `o1=o2=非零 ctx`。堆名来自 `RGX_*_HEAP_IDENT`（2.3 `rgx_heaps.h`），
  经 details 的 160B UMD 缓冲写入（BufSz=160，我方之前漏写）。
- 成功后新桥（T，零伪造即过，待按头文件补真值）：
  - `0x6:0x9 PHYSMEMNEWRAMBACKEDPMR` ×3（in72/out24，RAM 后备 PMR）；
  - `0x6:0x15 DEVMEMINTRESERVERANGE` ×3（in24/out12）；
  - `0x6:0x13 DEVMEMINTMAPPMR` ×3（in32/out12，设备映射）。
  另有 mmap×5/munmap×3（info 页之外的新映射行为，待分类）。
  56-op 成功序列见 `reports/umd-bridge-devmem-trace.jsonl`。
- 附带方法：ASan（exe+shim 同插）实锤 UMD 错误路径 double-free；
  `MTSRVFindHeapByName`/`DevmemFindHeapByName` 导出确认按名找堆；
  `Ext` 返回值语义：1=成功，0=吞错失败——只信 trace/dump。
- 给 Stage B 的输入（新增）：堆名表（`heap_names[]` 即上报值）；
  devmem 流程需要 named heaps（General/PDS/USC 至少三者）。

## 12. bA6：renderctx 签名与参数块，堆损坏的非确定性（进行中）

- 签名（S+gdb）：`RGXCreateRenderContext(conn?, params?, outptr=rdx)`；
  `RGXCreateRenderContextCCB` 要求 rsi 非空、`[rsi+0x30]/[rsi+0x34]` 非零、
  第 7 参数（out 句柄）非空——缺失报 `ppsRenderContext invalid` 返回 3。
- 参数块 `+0x10` 必须是子结构指针（零则在 `FindHeap("General")` 前 SEGV）；
  取 devctx 时行为非确定：SEGV / mutex 自死锁（单线程 `pthread_mutex_lock`
  等待）/ 返回 3——三态并存指向**堆损坏**（某伪造维度仍错）。
- 新桥（T）：`0x6:0x27 PVRSRVUPDATEOOMSTATS`（in8）——OOM 统计上报，
  说明某分配失败是 abort 前因之一。
- 方法：DebugPrintf 断点读参（`%s in %s()` 类消息直接报失败点）；
  `dumpat` 任意地址读；`bID*` 解引用传参；`call` 支持 8 参数。
  成功序列见 `reports/umd-bridge-renderctx-trace.jsonl`（58 ops，含新桥）。
- 给 Stage B 的输入（新增）：renderctx 三参数形状；params+0x10 子结构；
  OOM-stats 桥存在（KMD 需实现计数器接口）。

## 13. bA7：renderctx 堆名查找机制与名字生命周期问题（进行中）

- `RGXCreateRenderContext(conn, params, outptr=rdx)` 最远到达：
  PMR 分配 → `0x6:0x27 UPDATEOOMSTATS` → unref → 返回 1（outptr 未写）。
  58-op 序列见 `reports/umd-bridge-renderctx-trace.jsonl`。
- 按名找堆机制（S）：`0x94a60` 遍历数组（count@+0x18，表@+0x20），
  `entry+0` 为名字指针，`strlen/strncmp` 比对；devmem 期
  "PDS/General/USC" 三查全中（rdi=同一 registry）。
- renderctx 期同名查找失败 → `DevmemSubAllocate` 报 OUT_OF_DEVICE_VM →
  DCE 上下文 PDS 缓冲失败 → 返回 1。堆名指针追踪（`strat`/`dumpat`/
  `bID@@`/`bID@`/`*` 前缀多级解引用）显示 entry 名指向 mmap 区
  （0x77…，非堆 0x55… 非栈 0x7fff…），疑为 UMD 自有 arena 拷贝——
  生命周期待定是当前头号问题。
- 行为三态（SEGV/挂起/返回 1/3）同输入并存，堆损坏仍未排除；
  ASan 已抓到 GetAppHint 野写（features 野指针）等多个次生现象。
- 工具：`strat` 字符串读、`dumpat`、`bID*+OFF`/`bID@OFF`/`bID@@OFF`、
  `conn+OFF`、8 参数调用。

## 14. bA9：工具稳定性修复 + OOM-stats 定位（进行中）

- **重大纠正**：此前三态非确定性（崩溃/挂起/返回）主要源于
  shim 自身 malloc/calloc/realloc/free 拦截的竞态（`resolving` 非原子、
  tmp 回退、日志递归），而非 UMD 本义行为。现默认关闭拦截
  （`UMD_ALLOC_WRAP=1` 才开），连续运行已确定性复现
  （connect/devmem/renderctx-attempt 全程无崩溃无挂起）。
  此前在非稳定工具下得到的“堆损坏”结论**降级为待复核**；
  ASan 实锤的 devmem-teardown double-free 仍成立
  （ASan 下自有分配器，我方拦截被屏蔽）。
- 新桥：`0x6:0x27 PVRSRVUPDATEOOMSTATS`（in8=`aea50000 1e000000`，
  OOM 上报），位于 renderctx PMR 分配之后、unref 之前。
- PMR 伪造现状：句柄递增互异、`uiOutFlags=IN(0x1233)`、
  `isSystemMem=1`；仍触发 OOM 路径。下一步看分配后本地校验
  （import？map？ legislate OUT 字段？）。
- 给 Stage B 的输入（新增）：UMD 工具链本身必须确定性；
  OOM-stats 桥需实现（计数器语义）。

## 15. bA10：TA timeline 与 sync ioctl（renderctx 成功其一）

- `PVRFDSyncOpen` 的 `PVR_SYNC_IOC_RENAME`（`0x40206441`）在假 fd 上
  报 ENOTTY → 无 TA timeline → renderctx 失败。shim 对 DRM `pvr`
  系列 ioctl（`0x6440-0x6445`，桥包除外）一律回 0 后首次走通。
  （后续 bA10-device-conn 给出更完整的成功路径，本节保留 sync 发现。）
- 给 Stage B 的输入：KMD 必须实现整套 PVR sync ioctl
  （rename/create-fence/inc 等，不止桥包）；render 节点要能多开。

## 16. bA10：render context 创建成功（device connection 是钥匙）

- `PVRSRVConnectionCreateDevice(b7,u0,u0)` 跑出**第二套完整 Connect 序列**
  在 device-conn 上重做 devmem＋renderctx：
  > **bA24 订正**：这里的 `u0` 是**错的**，只是伪造 shim 下看不出来。
  > 该函数第 2 参是 **DRM 节点索引**；UMD 在 `0xa4af0` 处算 `index-0x80`
  > 并只接受 `0x80..0xbf`，所以 `0` 直接被拒，表现为
  > `MTSRV_ERROR_INIT_FAILURE(4)` 且**一条 ioctl 都不发**。
  > 打真驱动必须传本节点真实 render minor（如 `u130`）。
  **`RGXCreateRenderContext(...) -> 0`，outptr 非零 render ctx！**
- 120-op 成功序列见 `reports/umd-bridge-renderctx-trace.jsonl`：
  PMR/reserve/map 若干轮 → `0x82:0x8`（hRenderContext=0x6000）→
  `0x1:0x4`（0x7000）→ 第二节点 + `INIT(2)` → 两次 SYNC_RENAME → 返回 0。
  （通用 conn 上同调用只到 1；device conn 是渲染路径的前提。）
- 给 Stage B 的输入（新增）：KMD 必须支持**两种连接**
  （generic + device），渲染走 device；`PVRSRVConnectionCreateDevice`
  的线格式已在 trace 中（`b7 u0 u0` 即可）。
- 仍 open：CreateSyncPrim 在 `[conn+0]+0xa0`（features）野指针上 SEGV
  （通用/device conn 皆然，见 bA11）；Sync memType 动态值待其后。

## 17. bA8 附记：注册表内容（位置修正，内容保留）

- 注册表 dump：registry（count=11）entries 0-6 名为 "General"，7 为
  "PDS Code and Data"，8-10 为 "USC Code"。
- 机制：UMD 在每次 details→heapcreate 间隙**同步拷贝**名到堆对象；
  未命名堆继承共享栈槽的上次残留。名字稳定。
- `heap_names[]` 即 KMD 上报名（General/PDS/USC/Component Control 均已验证）。

## 18. bA11：SyncPrim 现状与 kick 路线图（进行中）

- `CreateSyncPrim(conn,...)` 在 `GetFeatures` 链
  （`[conn+0]+0xa0` 未初始化）SEGV；`[X+0xa0]` 从未被写入
  （watchpoint 全程无命中）——features 槽的写入者缺失，
  候选：第二 Connect 调用（0x927b4，疑为死代码，从未执行）、
  或某堆/特性桥。`PVRSRVConnectionCreateDevice` 已验证可用但不补该槽。
- 关键发现：renderctx 创建**内部**经 `0x7c0a4` 注册 sync 分配回调
  （`0xa0020` 存 `0x9f970`），真分配延迟到首次使用——
  故 `0x02:0x00` 在 kick 准备时才现身（memType 动态值须到 kick 才见）。
  三处注册点（`0x3bebb/0x74fc8/0x7c0a4`）可能传不同 memType，
  静态单链 `edx=0x2` 只覆盖其一。
- kick 入口候选（UMD 导出）：`RGXKickCDM/CDM2`、`musa_KickCETQ`、
  `MUSACESubmit`、`RGXCreateKickSyncContext`；TA3D kick 另查。
  推进需要：命令 BO、target、sync、ZS 全套构造——工作量大，
  且离线包无法用硬件消费验证（只能审包）。
- 给 Stage B 的输入（新增）：sync 分配是 lazy 的（注册与分配分离）；
  kick 前置为 renderctx + devctx + device-conn 三件套。

## 19. bA12：kick 入口与事件过滤器需求（Stage A 收官评估）

- kick 入口 `RGXKickTA(renderctx?, kickparams?, ...)`：
  参数块 `+0x30` 非空；首调用 `MTSRVGetClientEventFilter` 即崩
  （`[renderctx+0x50]` 未初始化，`[r14+rax*4+8]` 野读）。
- `[renderctx+0x50]`（事件过滤器对象）应在 renderctx 创建时建立，
  我方流程未建——属 UMD 内部对象图缺失，非线格式问题。
- 到此 Stage A 离线 triage 已覆盖：节点选择、Connect 全序列、
  Bvnc、info 页、堆表＋名、sync ioctl 全家、双连接、devmem 全流程、
  renderctx 创建、PMR/reserve/map、OOM-stats、kick 入口形状。
  剩余（kick 包内容、Sync memType 动态值、事件过滤器初始化）
  均需**真实 KMD 数据**才能继续——伪造边际收益已尽。

## 7. 下一步（Stage B 提案，待用户拍板）

最小内核桥（新 recovery/test 模块，不碰主模块会话）：
1. DRM 节点（version 名 `pvr`）+ `INIT(1/2)` + Connect（含 Bvnc其一、
   caps、arch）+ 7 个共享结构 ABI 对齐。
2. info 页（64KB @mmap `0x1001000`：设备数/`+0x44`/`+0x48`）+
   堆表（11 项真实范围＋名）+ PMR/import/reserve/map 真后备
   （复用现有 BO/VM/VRAM 代码）。
3. sync ioctl 全家（rename/create-fence/inc）+ 双连接支持。
4. 然后 UMD 跑真实数据：Sync memType 动态值、事件过滤器、
   kick 包捕获（审包对 PSC）自然落地。
5. 成功标准：UMD 走完 connect→devmem→renderctx→kick-submit
   全链路（先只审包不上交硬件），输出最小命令集终版。

## 20. bA13：Windows 灵感回灌 + Sync 动态值 + CreateSyncPrim 打通

### 20.1 bA12 勘误：kick 首参是连接，不是 renderctx

- `MTSRVGetClientEventFilter`（`decompiled.c:15822`，`0013fae0`）：
  `lVar1=*(param_1+0x50)` 的持有者是 `0xd0` 连接结构
  （`ConnectionCreate:13644` 分配），`+0x50` 由 `FUN_0013ede0`
  （`PVRSRVHWPerfUmInit`）经 `_AcquireHWPerfSetting` 写入；
  `conn+0x14=0x20` 时 bit `0x10` 为零必走分配路。renderctx（`0x330`，
  `RGXCreateRenderContextCCB:53459`）全程无 `+0x50` 读写。
- `RGXKickTA:53206`（`0017afd0`）：`param_1=psDevConnection`，
  `param_2=psKickTA`，门卫仅 `(param_2!=0 && *(param_2+0x30)!=0)`。
  bA12 误传 renderctx 作 param_1——调用错误，非对象图缺失。
- 实测（T）：`connect 0` 后 `conn+0x50` 非零、`conn+0x14=0x20`；
  `RGXKickTA(conn, kickbuf(+0x30≠0))` 越过 filter，止于
  `RGXPrepareTA(FUN_00178800)+0x253`（VMA `0x178a53`，gdb 双独立 run 定位）。
  下一步是整形 kickTA，不是补事件过滤器。

### 20.2 Sync memType 动态值：`0x100000000`（静态 2 证伪）

- `0x02:0x00` IN 8B=`0000000001000000`，对照 5.2
  `common_sync_bridge.h` 即 `MTGPU_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK.ui64MemType`。
- 机理（S）：`CreateSyncPrim:001782a0` 调
  `FUN_0019e0c0(arena,lVar3,1,0x100000000,lVar3,"Sync_Prim",...)`；
  `RA_Alloc` 把 `param_4` 掩码（`& 0xf8e0007f0c1eff33`，bit32 保留）后透传给
  `SyncPrimBlockImport(FUN_0019f970):77234 → wrapper(FUN_001390d0):12045`。
  静态 `edx=0x2` 是 `FUN_001a0020:77446` 建 RA 的类别参（`FUN_0019da20` param_3），
  与桥输入无关——bA1/bA5 结论作废。
- 因果对照（T）：同会话去手动 `CreateSyncPrim` 则零 `0x02:0x0x`、零 DebugPrintf；
  加上即现（两次独立 run 同值）。

### 20.3 `CreateSyncPrim → 0` 会话配方与 sync 伪造

- 最小成功序列（单进程，`build/probe/umd_connect_harness`）：
  > **bA24 订正**：`u0` 应为真实 render minor（`u130`），见上。
  `connect 0` → `PVRSRVConnectionCreateDevice(b7,u0,u0)` →
  `RGXCreateDeviceMemContext(b7*,b5,b5+8)`（`o1=o2=0x30` RGX ctx，
  `*param_2=*param_3=__ptr`，见 `0016f970` 尾）→ params（`+0x10=devctx` 直接指针、
  `+0x30/+0x34≠0`）→ `RGXCreateRenderContext(b7*,b6,b9) → 0，out 非零`。
  注：`+0x10` 是 devctx 本体（`CCB` 内 `__ptr[1]=*(param_2+4)`，
  注册表=`*(__ptr[1]+8)`=`MTSRV ctx`经 `FUN_001417e0→FUN_00195290` 填入）；
  bA6 的「子结构」说修正。
- sync 三槽（T）：`generic-conn+0xb0`、`device-conn+0xb0`、`renderctx+0x30`
  皆非零且 arena（`+0x40`）皆非零（一次「arena=0」系 harness `'*…'` 引号误包，
  复测已澄清）。
- `CreateSyncPrim(device-sync)` 曾返 3：根因是全零 `0x02:0x00` 输出
  （`BlockSize=0`）触发后续 `RA_Alloc` 入口拒零（`0x652`，dprintf 抓获）。
  shim 新增 `fabricate_sync_alloc`（`hSync=0x60xx`、`PMR=0x5000+`、
  `BlockSize=0x1000`）后**首次返回 0**，out 非零；新桥
  `0x02:0x07 BridgeSyncAllocEvent` 现身（IN 20B，待对头文件精读）。
- Stage B 硬输入：Sync 真 `memType=0x100000000`；`0x02:0x00` OUT 必须非零
  句柄/PMR/BlockSize。

### 20.4 Windows 侧 mining（反编译文本直读，不启动 Ghidra）

- `RenderContext/KickTA/EventFilter/SyncPrim/PSC` 在 `mtkm64.sys/mtdxum*/mtgfxc*`
  的 `decompiled.c` 基本零命中；对应物：WDDM Cb
  （`mtdxum64.dll:261885ff D3DDDIRenderCb/CreateContextCb/
  Create/Destroy/Wait/SignalSynchronizationObject*Cb`）与
  `musa::compiler` LLVM 后端（`mtgfxc64.dll` 内
  `llvm.musa.load.vertex.buffer / get.vertex.id|base / per.vertex.buffer.base`）。
  PSC 上下文 ctor（Linux `001a4fc0`，`0x428` 字节）在 Windows 侧无明文对应——
  顶点取数手写重实现无望，桥接是唯一活路。
- `mtkm64.sys` strings：FW 内嵌 `KICK-DM/PreKick/Serial-Kick/Kick-Task/
  CDM_DM KICK/GEOM_DM KICK/GFX-Stage …/Kick Event update|check|Timeout +
  CDM Context Store/Resume RGX_CR_CDM_CONTEXT_STORE0/1…`（`0x14104xxxx` 段，
  `references_from:[]`，查 `disassembly.txt` 反查 `LEA`）。
- `InitMTFeatures(00151930)` 按 device-type 选表：`0=Sudi，10=QuYuan1，
  0x14=QuYuan2，0x28=PingHu1` + `PVRSRVGetMultiCoreInfo` 回填。
- `FUN_00183d30=RGXGenerateContextSwitchUniformTasks`（Linux `58016`，
  不在 `mtkm64.sys`）：`local_40[0]==0`（USC `calloc(8)` OOM）即
  `Failed to create USC task`；`GeneratePDSUniformLoad` 要求 state-buffer
  addr 非零、code/data 段尺寸非零——Stage B 合成描述符时的非零清单。

#### 20.6 bA14：ZS 缓冲参数序修正 + 伪造成就的又一面墙

- harness 扩容：`call` 支持 16 参数（`RGXCreateZSBuffer` 有 13 个），新增
  `str ID OFF TEXT` 写串（`MTSRVFindHeapByName` 需要名缓冲）。
- **`RGXCreateZSBuffer` 真实参数序**（`00184f90` + 内联体 `MTSRVAllocExportableDeviceMemMIW`
  `00144840` 交叉确认）：
  `(psDevConnection, hHeap, psDevMemCtx, uiFlags, psMalloc, uiSize(log2),
    bAligned, bCPU, ppui64Mem, pphMemHandle, pppsZSBuffer, pphPMR, pvClientData)`
  —— **`param_2` 才是连接**，`param_1` 是 hHeap；bA14 之前按相反顺序调用，
  报 `0x1d3 "%s invalid" psDevConnection`。
- 堆对象布局（T，`MTSRVFindHeapByName(devmemctx,"General") -> 0` 后 dump 96B）：
  `+0x00` self、`+0x08` refcount=1、`+0x10` base（`0x40000000`）、
  `+0x18` size（`0x8000000000`）、`+0x20` reserved=0、`+0x50` Log2PageSize=12。
  该 `+0x50` 是 `MTSRVGetHeapLog2PageSize`（`FUN_001972c0`）唯一来源。
- 新墙：参数序修正后 `RGXCreateZSBuffer` 进入
  `DevmemAllocateExportable`（`FUN_00195660`）并在其内部校验返回
  `MTSRV_ERROR_INVALID_PARAMS`（`0x6e3` dprintf 定位）。该层依赖真实堆几何
  （size/align/flags/Log2AllocSizePage 与三处互斥标志），伪造不再收敛——
  与 §19 结论一致：**ZS/kick 的剩余工作必须由真实 KMD 数据驱动**。
- 结论：离线阶段能给的已给完（连接/双连接/devmem/renderctx/sync 全绿，
  memType 实值在手）。下一步应转入 Stage B 内核桥实现。

## 20.5 工具链教训

- devmem 成功后 UMD 工作线程空转 `read(fd3)`：shim 原逐条记日志，
  一次刷出 4.7GB/8100万行（已删）。现 `read` 日志默认关闭
  （`UMD_TRACE_READ=1` 才记）；成功会话进程不自然退出，一律 `timeout`。
- `probe/umd_connect_harness.c` 未提交的 `poke` op 一并入库（bA13 commit）。
- 大 trace 不落 `reports/`；只记配方、线格式与结论。
