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

## 7. 下一步（bA7）

1. 堆损坏根因：用 ASan 跑 renderctx 组合（exe+shim 同插已有先例），
   抓第一次非法写（候选：params+0x10 子结构内容？堆名？import 句柄？）。
2. 然后：Sync alloc 动态现身（复核 memType=2）→ `RGXKICKTA3D3`
   （看清尾部 8 字节）→ 最小命令集 + 打桩表（Stage B 输入）。
3. 回填 `0x6:0x9/0x15/0x13` 的真值伪造（按 2.3 头文件结构体）。
