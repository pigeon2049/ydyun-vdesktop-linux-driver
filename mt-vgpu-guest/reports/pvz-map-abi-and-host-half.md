# PVZ firmware heap 映射 ABI 与 Host 半边对照（2026-09-28）

分析输入为 2.3.0 官方包 `mtgpu_core.o_binary`（SHA-256
`694768574d78a724fcfb5b2a2ecbbeac34828c861eb76ee1bb7dd2ed996450a0`）和 Native
2.7.1 `mtgpu_core.o_binary`（SHA-256
`83398c8c24cace69e53d561bd62ce6725a1ae2776b160ffbf67fc5fae9f892c3`）。下述结论来自
符号定位后的 ELF 反汇编和随包头文件，不来自运行时试发 PVZ 请求。

## Guest 请求 ABI

官方 2.3.0 core 的 `PvzClientMapDevPhysHeap` 位于
[`host-2.3-core.asm`](host-2.3-core.asm) 的 `0xf1000`。反汇编显示它从
`PhysHeapGetDevPAddr` 取得 heap 的设备物理地址，然后在 PVZ 锁内调用 client
table：`funcID=1`、`devID=0`、`size=0x800000`（8 MiB）、`PAddr=heap DevPAddr`。
`PvzClientUnmapDevPhysHeap`（`0xf1110`）传入 `funcID=2`、`devID=0`。

这些实参与两份版本的 [`vmm_impl.h`](../src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0/inc/pvr/services/vmm_impl.h)
定义一致：client map 为 `(funcID, devID, size, guest PAddr)`；server map 另外
接收由 VM manager 提供的 OSID。头文件明确规定 Host PVZ handler 由 IMG 提供，
Guest PVZ 回调由第三方按具体 VMM 提供；Guest IPA 必须由 VM manager 转换/映射到
Host IPA 再交给 Host，没有 provider 时应返回 `PVRSRV_ERROR_NOT_IMPLEMENTED`。

官方 2.3.0 core 的 `RGXFwRawHeapAllocMap`（[`host-2.3-core.asm`](host-2.3-core.asm)
地址 `0xb0ab0`）验证 size 必须正好为 `0x800000`、PAddr 非零、OSID 小于 15，再按该
地址和大小创建 LMA physical heap。这与 2.3.0 client 请求完全吻合，给出了此版本 Host
heap handler 可接受的明确参数约束。

## 本地 Native 2.7.1 core 的 Host/server 半边

[`native-2.7-pvz-server.asm`](native-2.7-pvz-server.asm) 保存了 Native 2.7.1 的
client/server map/unmap、`PvzServerVMMConfigure` 和 Guest stub 反汇编；
[`native-2.7-pvz-rawheap.asm`](native-2.7-pvz-rawheap.asm) 保存其
`RGXFwRawHeapAllocMap`。可确认：

- server map 要求 `devID=0`、`funcID=1`，检查 OSID 对应 VM 在线，然后把 OSID、Guest
  请求的 size 和 PAddr 交给 `RGXFwRawHeapAllocMap`；成功后读取新建的 firmware heap
  信息并把该 OSID 的固件状态置为在线。
- server unmap 要求 `devID=0`、`funcID=2`，检查 VM 在线，将固件状态切到离线，再调用
  `RGXFwRawHeapUnmapFree`。
- 该版本的 client map 默认请求 `0x10000000`（256 MiB）；对应的 Native raw-heap
  helper 也严格要求 256 MiB 和 `OSID=0`。它的 `config_kernel.h` 定义
  [`RGX_NUM_OS_SUPPORTED=1`](../src/mtgpu-2.7.1-6.12/inc/config_kernel.h)，属于 Native
  单 OS 构建。与官方 2.3.0 的 8 MiB / 多 OS 契约不同，不能把该 Native server/helper
  当成 S3000 Guest 的兼容 Host 实现。
- 更关键的是，Native 2.7.1 自身的 `StubVMMMapDevPhysHeap` 和
  `StubVMMUnmapDevPhysHeap` 仍返回 `0x0a`（`PVRSRV_ERROR_NOT_IMPLEMENTED`）。因此该
  core 提供的是可被 Host PVZ server 调用的 Host 半边，并没有提供 Guest→Host 请求
  transport。

官方 2.3.0 候选的两个 PVZ server 入口固定返回 `0x152`
（`PVRSRV_ERROR_INVALID_PVZ_CONFIG`）；其 raw-heap helper 虽然能按 8 MiB 建立
Host heap，但当前候选没有可用的 PVZ server 配置。Native 2.7.1 则有不同的 256 MiB、
单 OS raw-heap 路径。两份 core 都不能替代与目标 S3000 Guest 匹配的实际 VM
manager/provider：Guest stub 需要把 map/unmap 请求跨 VM 转发；Host server 再按 OSID
为固件 heap 执行分配、状态转换和释放。当前 Linux Guest RPC helper 没有这个操作，
Windows 参考驱动反编译也未识别到相应代理。通用 Hyper-V hypercall 能力位不包含 MTT
PVZ 服务 ABI 或 Host 端处理逻辑。

## 链接候选的 PVZ dispatch table 重定位复核（2026-09-29）

在新离线候选 `build/official-vgpu-2.3.0-guest-heapcount-fwbasepubfix-20260929/mtgpu.ko`
（SHA-256 `d0ba818d96dba200e433c416cbcf918862d0a920859338760e4b09abc1f7002b`）时，不能只按
原始 `objdump -d` 显示的立即数解释 `VMMCreatePvzConnection`（`0x121bd0`）。该函数
`mov [rdi],0` 操作数带有 `R_X86_64_32S .data+0x1b9a0` 重定位；`.data+0x1b9a0` 正是
`gsStubVmmPvz`（56 字节）对象。重定位应用后，构造函数把本地 dispatch-table 地址写进
out-pointer并返回 `PVRSRV_OK`。`PvzConnectionInit` 因此通过非空检查、设置初始化标记并
返回成功。先前把未重定位的立即数当成运行值、据此声称连接为空，是错误解释。

同一对象的重定位表显示 client map/unmap 两项分别指向 `StubVMMMapDevPhysHeap`
（`.text+0x121bb0`）和 `StubVMMUnmapDevPhysHeap`（`.text+0x121bc0`），固定返回
`0x0a`（`PVRSRV_ERROR_NOT_IMPLEMENTED`）；server map/unmap 则指向 `0x121a40/0x121a80`，
固定返回 `0x152`（`PVRSRV_ERROR_INVALID_PVZ_CONFIG`）。所以本地 PVZ 初始化虽然可以成功，
这张表仍是占位实现，没有把 Guest 请求送到 VMM 或 Host 的 transport。结合
`RGXInitCreateFWKernelMemoryContext` 的 Guest 静态 premap 分支，它会跳过该函数内的动态
raw-heap map 循环；这支持“该特定固件初始化路径不依赖动态 map”的判断，但不能证明运行态
其它代码绝不触发 map/unmap。此项检查只读链接模块，未加载模块或访问 PCI/GPU。

## 适配结论

这组对照描述的是动态 Guest heap map/unmap ABI 缺少 transport 与 Host receiver 的情况。
仅在 Guest 把 stub 改成成功会让固件以为映射已建立，却不会完成 Guest IPA 到 Host IPA
的可访问映射；在未知 hypercall 上发送猜测参数也无法安全验证。之后对
`RGXInitCreateFWKernelMemoryContext` 的控制流核查发现，官方配置的 static Guest 分支会
把 FW_MAIN/FW_CONFIG 标为 premap，并跳过该函数里的 `RGXFwRawHeapAllocMap` 循环。因此
此 PVZ provider 缺口尚不能认定为当前静态 Guest 启动必经阻塞；如果其它运行分支实际
调用 map/unmap，仍需要对应 provider/Host 服务契约。静态 premap 也未证明 BAR2 backing
和 Guest OSID 槽转换正确。详见 [静态 Guest FW premap 控制流](static-guest-fw-premap.md)。
本轮没有操作设备、加载模块或触碰宿主。

## 官方 2.3.0 直接调用点全量复核（2026-09-29）

新增 `scripts/verify-pvz-call-path.py` 扫描已保存的官方 core 反汇编重定位和随包 C/H/S/Makefile：
`RGXFwRawHeapAllocMap` 仅有 `0xba5f1` 一个直接调用点，位于
`RGXInitCreateFWKernelMemoryContext` 的 Host 分支；`RGXFwRawHeapUnmapFree` 位于
`0xba7b1` setup 失败清理和 `0xba862` deinit。Guest deinit 在 `0xba843` 的模式判断跳过
Host unmap。两个 `PvzClient*DevPhysHeap` wrapper 均没有直接重定位调用点，源码扫描也未找到引用。

因此，直接静态调用图没有显示 Guest 启动会进入缺失的 PVZ map/unmap transport；这比单函数
premap 核查更强，但仍受不透明间接调用、外部 provider 或运行期注册的限制。它不证明 Guest
FW heap 有效映射到 BAR2，也不证明 PVR E1C premap 窗口对应 Host OSID 槽。校验器输出会保留
这些边界，且明确记录没有访问硬件。
