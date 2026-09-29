# Linux Guest 静态 FW premap 路径

日期：2026-09-29。依据官方 Linux 2.3.0 `mtgpu_core.o_binary` 的
`reports/host-2.3-core.asm`、随包配置和公开头文件静态核对；没有加载模块或访问硬件。

在 `RGXInitCreateFWKernelMemoryContext` (`0xba460`) 中，Guest driver mode 的值为 1
（`pvrsrv_device.h` 的 `DRIVER_MODE_GUEST`）。Guest 分支跳到 `0xba729`，先确认模式仍为
Guest，再对 FW_MAIN 与 FW_CONFIG 两个 heap 分别调用 `DevmemHeapSetPremapStatus(1)`。
同一函数中的 `RGXFwRawHeapAllocMap` 调用位于 `0xba5f1`，在 Guest 分支跳转目标之前，
因此这条静态 Guest 路径会绕过该 Host raw-heap 分配/登记循环。

进一步核对了本机已记录的 Guest OSID 7：随包配置支持 15 个 OSID，公开 heap ID 中
`FW_PREMAP7=18`、`FW_PREMAP0=11`，`_SelectDevMemHeap` 以两者差值 7 选择
`psGuestFirmwareRawHeap[7]`。Host `RGXRegisterDevice` 也确实生成 15 个连续 8 MiB
蓝图，所以 OSID 7 的静态 premap 设备地址为 `0xe1c3800000`。校验器现会验证 OSID 数、
heap ID、数组索引和 blueprint 循环，并用回归测试拒绝把 OSID 7 移出范围或改错 heap ID。

该路径分别预映射 FW_MAIN 与 FW_CONFIG；同版 PVR 头文件将 FW_CONFIG 明确标成 FW_MAIN
的子堆，`_SelectDevMemHeap` 则把 CONFIG 分配路由到 `psFirmwareConfigHeap`。因此 Guest
需要的是有效的 FW_MAIN 父堆基址，再由 PVR 配置子堆；没有证据表明信息页还应提供另一份
独立 FW_CONFIG 基址。Host 槽校验器现在会检查这个公开 ABI 关系，避免遗漏 config 子堆。

官方构建定义了 `RGX_VZ_STATIC_CARVEOUT_FW_HEAPS`、`RGX_FW_HEAP_SHIFT=23` 与
`PVRSRV_VZ_BYPASS_HMMU`；`rgxfwutils.h` 在 static carveout 配置下使用共享固件连接状态，
并将 `FW_PREMAPn` 选择为 `psGuestFirmwareRawHeap[n]`。结合 `RGXRegisterDevice` 建立的
`0xe1c0000000 + OSID * 8 MiB` heap blueprints，这与 OSID 固定预映射方向一致。

沿 OSID 4 Windows 参考记录和 QY1 拓扑假设推算，Host OSID 7 槽的 BAR2 aperture offset
候选为 `0x40800000`，Guest `fw_heap_base` GPU/card PAddr 候选为 `0x7737ef000`；相对 OSID 4
的步进分别都是 `0x1800000`。`0xe1c3800000`、`0x40800000` 和 `0x7737ef000` 属于不同
地址域，这种步进相同不能证明它们互相转换或实际指向同一 backing。

`_GetPremappedVA` 进一步通过 `PhysHeapGetDevPAddr` 和 `PMR_DevPhysAddr` 计算 PMR 相对
PhysHeap 基址的偏移，再将偏移并入 E1C 窗口。该 helper 保留堆内偏移，不访问 Host
BAR2，也不实现 Guest IPA 到 Host IPA 的转换。`verify-static-guest-fw-premap.py` 现在会
核对 blueprint 的 E1C 基址/8 MiB 步进、OSID 7 blueprint 与 heap ID，以及 helper 的相对
偏移计算。

这修正了前一轮“PVZ map stub 是已确认阻塞”的判断。进一步对官方 core 的直接 ELF
重定位做全量扫描后，`RGXFwRawHeapAllocMap` 只有上述 `0xba5f1` 一个调用点；两处
`RGXFwRawHeapUnmapFree` 调用分别是该 setup 路径的失败清理和 Host context deinit，Guest
deinit 分支会跳过后者。`PvzClientMapDevPhysHeap` / `PvzClientUnmapDevPhysHeap` 没有直接
重定位调用点，随包官方 C/H/汇编/Makefile 中也没有这两个 wrapper 的引用。当前证据因此
不支持把 PVZ stub 认定为这条静态 Guest 固件路径的必经阻塞；但重定位与源码扫描不能排除
不透明的间接调用、外部 provider 或运行期注册路径。

即使该固件路径通过 premap 初始化，PVR premap device address 到 Host BAR2 OSID 槽之间的
转换、映射及其实际物理 backing 仍没有静态闭环或实机证据。完整扫描可运行
`python3 scripts/verify-pvz-call-path.py`；该检查只覆盖直接 ELF 重定位和所列官方源码文件，
不访问设备。

可运行 `python3 scripts/verify-static-guest-fw-premap.py` 重验该分支结构。脚本同时核对
Guest 分支越过 raw-heap map 调用、检查 Guest mode，并执行两次 premap-status 调用；
测试还会在人为把跳转目标改回 map 调用区时拒绝结果。该核验只证明反汇编控制流，不
证明 Guest heap 的实际物理 backing 或 GPU firmware 已运行。
