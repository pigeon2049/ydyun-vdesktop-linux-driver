# Host 固件堆 ring buffer 路径核查

日期：2026-09-29。分析对象为 Linux 2.3.0 Host 包中的
`mtgpu_core.o_binary`，只使用现有符号表、反汇编和随包头文件；没有调用驱动或访问设备。

## 符号与调用关系

`mtgpu_mdev_init_fw_heap_ring_buffer` 位于 `host-2.3-core.asm` 的 `0x53b80`。
反汇编显示它只在 `mtgpu_load_windows_firmware` 为真时继续；随后按驱动数据中的
MPC/OSID 范围计算 BAR2 地址。每次 `os_ioremap` 长度为 8 MiB，成功后只执行
`os_memset(mapping + 0xc0, 0, 0x39540)`，再 `os_iounmap`。这里没有读取 Guest 请求、
写响应状态、处理中断，或调用 PVZ server map/unmap 函数。

反汇编的唯一直接调用点位于 `LoadWindowsFirmware` (`0x978a0`) 的 `0x979e1`，在它
初始化 `0x64250` 处的六项状态数组之后、查询 OSID 范围并继续设置 Windows 固件数据之前。
该顺序和函数内容支持“初始化 BAR2 内固件相关区域”的解释；它本身不构成 Guest
heap 注册服务。

通用 `ringbuffer_ops` 是另一套实现：其初始化引用出现在
`mtgpu_fec_ipc_init_prepare` (`0x45500`) 与 `mtgpu_ipc_add_device` (`0x47460`) 周围，
函数指针表包含 `ringbuffer_get/put` 等操作。`mtgpu_mdev_init_fw_heap_ring_buffer`
没有调用这些函数，也没有引用这张表。因此不能仅凭函数名把它解释为 Guest↔Host
PVZ 控制队列。

## 与目标 Guest 的关系

随包旧版 `struct vgpu_info` 确实包含 `fw_heap_base/fw_heap_size` 和
`mmu_heap_base/mmu_heap_size` 字段。但本机从 Windows 参考路径保存的 version-2
信息页是另一种布局：它使用 24 字节段记录，固定信息与扩展区位置由 Windows 反编译
恢复，不能将旧结构字段偏移直接套到该页面。当前记录也没有取得由这条 Host
`LoadWindowsFirmware` 路径产生、可供 Linux Guest 使用的 `fw_heap_base` 映射证明。

Linux 2.3.0 Guest 的 `PvzClientMapDevPhysHeap` 仍以 `(funcID=1, devID=0, size=8 MiB,
Guest physical address)` 请求映射；`vmm_impl.h` 要求具体 VM manager 把 Guest IPA
转换到 Host IPA 并提供 Host 接收端。上面的 Host 函数没有该请求/应答语义，Windows
RM 的本地 `MmMapIoSpace` 路径也不能补上这层转换。

## OSID 固件堆槽与 Windows v2 信息页的交叉核对

继续追踪 `vgpu_device_info_init` (`0x4c570`) 后，看到它也使用相同的 MPC/OSID 公式，
并为每个 vGPU 填入固定 8 MiB 的固件堆地址。结合 `mtgpu.h` 对 `osid_start`、
`osid_count` 的定义，两个 Host 函数使用的 BAR2 槽位可写成：

```text
stride = osid_count * 8 MiB + 10 MiB
slot   = mpc_card_base + mpc_id * stride
       + (guest_osid + 1 - osid_start) * 8 MiB
```

`mtgpu_mdev_init_fw_heap_ring_buffer` 按这个 `slot` 做 8 MiB `ioremap`；
`vgpu_device_info_init` 把同一个槽作为 vGPU 固件堆地址，并记录 8 MiB 大小。
这让“该函数只是在清一段无关 BAR2 内存”的解释不再成立：它处理的是 Host 为
MPC/OSID 划分的固件堆槽，但仍然没有 map 请求、应答或 PVZ server 调用。

继续追 `vgpu_device_info_init` 的 PAddr 来源后，看到
`mtgpu_vgpu_get_mpc_mem_card_base` 用 GPU 显存卡基址加 MPC stride，再加 OSID heap 槽；
其公式与 BAR2 槽具有相同的每 OSID 8 MiB 步进，但结果是 GPU/card 物理地址，不是
BAR2 aperture offset。Host 的 Linux v1 `fw_heap_base +0x848` 发布的是这个每 vGPU
记录里的 PAddr；Guest 后续把该字段放入 `PHYS_HEAP_CONFIG.sCardBase` 的 FW_MAIN 项。

Windows v2 OSID 4 参考页记录 FW 段 GPU 地址 `0x771fef000`，而保存的上传核验使用
BAR2 offset `0x3f000000`。在公开 QY1 拓扑假设（`osid_start=4`、`osid_count=5`、
`mpc_id=0`）下，反推得到 GPU `gpu_mem_card_base` 候选 `0x7717ef000`；沿 Host 公式
推算 Linux Guest OSID 7 的 FW_MAIN PAddr 候选为 `0x7737ef000`。同一 OSID 7 的 BAR2
槽仍是 aperture offset `0x40800000`，二者相差 `0x1800000` 的 OSID delta 相同，但
数值属于不同地址域。OSID 7 的 Linux v1 原始信息页并未保存，因此这个 PAddr 是从
Windows OSID 4 锚点和 Host 静态布局推导的候选，不是当前 Guest 实测值。

进一步核对 `vgpu_alloc_osid` 和 `mtgpu_mdev_gpa_to_hpa`：Host 为每个 vGPU 分配
`0xf0` 字节的私有记录表，转换函数按 `0x30` 字节步长检查 5 项。`vgpu_device_info_init`
将上述 8 MiB 固件槽写入表项 1 的基址字段（表首偏移 `0x30`），将大小 `0x800000`
和有效标志写入同一项。ring-buffer 初始化先取 PCI BAR2 resource start，再把相同的
MPC/OSID 槽偏移加到该基址并 `ioremap` 8 MiB。新增核验脚本
`python3 scripts/verify-host-vgpu-fw-slot.py` 检查这些保存反汇编中的字段写入、步长和
映射长度。它确认 Host 有相应的每 vGPU 槽记录和 BAR2 槽初始化路径；这个私有表不是
Guest `FW_PREMAPn` 地址到 Host BAR2 的转换证明，两个地址域之间的映射仍未证实。

同一静态核验还追到 Linux Guest 使用的旧版 `struct vgpu_info`：按随包头文件布局，
`fw_heap_size` 位于信息页偏移 `0x850`，`segment_info[DEVICE_TYPE_VVPU].size` 位于
`0x438`。`mtgpu_device_memory_fixup` 的 Guest 分支读取 `fw_heap_size`，并把
`[r12+0x10e0]` 交给 `mtgpu_platform_data_vz_init`，后者在 Guest 模式写入
`mtgpu_platform_data.vz_data.fw_heap_card_base`（总偏移 `0x50`）。反汇编表达式显示，
写入该字段的表达式会减去此前加上的 BAR2 起始地址和同一个 `fw_heap_size`，所以
`fw_heap_size` 在这个字段的最终表达式中抵消；不能把它说成该基址的直接来源。原始
`flag` bit 0 分支会把 `0x438` 的 VVPU 段大小装入该表达式，其余分支使用此前保存在
设备结构中的值。Guest 侧离线候选把该条件分支改为读取 Host v1 `fw_heap_base +0x848`，
其语义应是 Host 为该 vGPU 发布的 GPU/card PAddr，不是 BAR2 aperture offset。Host 静态
公式与 Windows OSID 4 参考锚点给出 OSID 7 的候选值，但现存 Linux OSID 7 记录没有保存
旧版信息页的原始 `flag`、`0x438` 或 `0x848` 字节，因而仍无法确认该 Guest 实际收到的
基址。它与 PVR `FW_PREMAPn` 设备地址的运行时 backing 关系也尚未实证。

Host 上游来源现在也已闭合：`vgpu_alloc_vram` 把 per-vGPU `+0xa60` 作为
`vgpu_calculate_vpu_mem_size` 的输出参数；`vgpu_device_info_init` 把该值写到
`vm_bar2_actual_mem_size`，而 `vgpu_access_pci_bar1_region` 在 VPU flag bit 0 置位时，
又把它写到 VVPU 段的 `size` 字段 `+0x438`。因此这是一个明确的“大小字段被用作基址”
语义错配：Guest 随后把 `+0x438` 送入 `fw_heap_card_base`，最终进入 PVR
`PHYS_HEAP_CONFIG.sCardBase`；配套 Host 却另行从私有 8 MiB 记录发布固件堆基址到
`fw_heap_base +0x848`。静态证据支持 Guest VPU 分支应读取 `0x848`，但仍不能证明本机
OSID 7 当前 flag 值、字段数值或 Host BAR2 backing。

第一版 `MTGPU_GUEST_PATCH_FW_HEAP_BASE=1` 候选曾把
`mtgpu_device_memory_fixup+0x447` 的 load 从 `[rdx+0x438]` 改成 `[rdx+0x848]`。后续
数据流复核发现 `+0x438` 是 VVPU 大小，并参与 VPU 范围运算；把它换成物理地址会破坏
长度计算。因此旧 `heapcount-fwbasefix` 模块已撤回，不能安装或加载。

当前默认关闭候选改在 Guest 的 `mtgpu_platform_data_vz_init+0x8d`：Guest 信息页指针先
从设备结构加载并保存至平台数据 `+0x70`，随后候选从该指针读取 `fw_heap_base+0x848`，
写入 `fw_heap_card_base`（平台数据 `+0x50`）。`mtgpu_device_memory_fixup+0x447` 的
`[rdx+0x438]` VVPU 大小读取保持原样。后链接脚本核对目标函数、构建标记、原指令和未改动
的 VVPU load，并保留 ELF 指令长度。该候选不改 Host ABI，也不证明 Guest `FW_PREMAP`
到 Host BAR2 的映射；目前只完成离线构建与静态核验，未做运行态验证。

继续追到 PVR 的 `SysDevInit`：它从平台数据 `+0x50` 取该 `fw_heap_card_base`，写进
首个 `PHYS_HEAP_CONFIG.sCardBase`（按公开结构偏移 `0x20`），并把 usage flags 设为
`0x10`，即 `PHYS_HEAP_USAGE_FW_MAIN`，描述符大小为 8 MiB。这证明本地 VZ 平台字段
确实进入 Guest PVR 的 FW_MAIN 物理堆配置。另一方面，PVR
`RGXRegisterDevice` 会独立创建 `FW_PREMAPn` heap blueprint，地址从
`0xe1c0000000 + OSID * 8 MiB` 生成；当前静态证据仍没有证明这个 blueprint、该
`PHYS_HEAP_CONFIG.sCardBase` 与 Host BAR2 OSID 槽之间的转换或 backing 关系。核验脚本
会检查 `SysDevInit` 对 `sCardBase` 的写入，并继续把这三个地址层分开报告。

本机保存的 Windows v2 信息页报告 OSID 4，固件段从 BAR2 偏移 `0x3f000000` 开始，
段大小 64 MiB；独立固件上传验证也记录 GPU 地址 `0x771fef000`、BAR2 偏移
`0x3f000000` 和 8 MiB 分配。若按公开头文件中的 QY1 SR-IOV 示例代入
`osid_start=4`、`osid_count=5`，且该单 MPC 设备的 `mpc_id=0`，Guest OSID 4 槽就是
`mpc_card_base + 8 MiB`。要得到记录的 `0x3f000000`，`mpc_card_base` 应为
`0x3e800000`。这个基址是**由已知槽位反推的候选值**，不是本机直接读取的 Host
字段；`osid_count=5` 也是公开 SR-IOV 示例参数，不是本机捕获的拓扑值。计算和独立记录
的交叉核对可用 `python3 scripts/verify-fw-heap-slot.py` 重现。

需要区分两份 Guest 身份记录：Windows v2 参考页是 OSID 4，而 Linux 侧此前保存的
Guest 信息页为 OSID 7。它们不是同一个已证明的运行时快照，所以 OSID 4 的上传记录
不能当成当前 Linux OSID 7 已选中该槽的证据。沿用同一 Host 拓扑假设，OSID 7 的候选
槽是 `0x40800000`；它位于 Windows 页报告的 64 MiB 固件段内，但当前没有 OSID 7 的
独立 PhysHeap PAddr 或上传记录。

另一个一致性线索是官方 Guest 配置包含 `RGX_VZ_STATIC_CARVEOUT_FW_HEAPS`，而
`pvrsrv_memalloc_physheap.h` 定义了按 Guest OSID 编号的 `FW_PREMAP` heaps，OSID 4
对应 `PVRSRV_PHYS_HEAP_FW_PREMAP4`。新的控制流核查显示，在 `RGXInitCreateFWKernelMemoryContext`
的 Guest 分支中，core 会将 FW_MAIN/FW_CONFIG 标成 premap，并跳过该函数内的 Host
`RGXFwRawHeapAllocMap` 循环。因此这段 Guest 路径不会执行该 Host raw-heap 登记循环；
但不能据此断言 `PvzClientMapDevPhysHeap` 客户端回调也不会从其它初始化代码触发。
该回调及失败 stub 仍存在，实际调用条件尚未逐一核对。当前资料也没有证明 Guest PVR
allocator 选中的 premap 地址已映射到 Host BAR2 槽，或证明 Host 已登记固件状态。完整
分支证据见
[静态 Guest FW premap 控制流](static-guest-fw-premap.md)。

当前 Linux Guest 的记录 OSID 是 7；对应的头文件枚举项是
`PVRSRV_PHYS_HEAP_FW_PREMAP7`，OSID 4 的 Windows 参考则对应
`PVRSRV_PHYS_HEAP_FW_PREMAP4`。两者都在该构建的 0–7 预映射编号范围内，OSID 7 的
候选槽计算已加入同一校验脚本，但其地址目前只是沿用 OSID 4 反推基址的预测。

## Linux PVR 预映射地址域

进一步核对 Guest 配置与 core 的 `RGXRegisterDevice`：`RGX_FW_HEAP_SHIFT=23`，固件
raw heap 基址为 `0xe1c0000000`；核心为 `FW_PREMAPn` 按 `base + n * 8 MiB` 生成
heap blueprint。`rgxfwutils.h` 随后把 `FW_PREMAPn` 映射到
`psGuestFirmwareRawHeap[n]`。因此 OSID 4/7 对应的**PVR 设备地址候选**分别是
`0xe1c2000000` / `0xe1c3800000`。`RGX_VZ_STATIC_CARVEOUT_FW_HEAPS` 还会选择共享固件
连接状态路径；这些是静态配置/预映射线索，不是运行态 Guest 地址读数。

继续查看 `_GetPremappedVA` 的反汇编：它先取 PhysHeap 设备基址，再通过
`PMR_DevPhysAddr` 取得分配的设备物理地址，计算两者之差，最后把该偏移并入
`0xe1c0000000` 窗口。这说明 PVR 在预映射窗口里保留了 PMR 相对 PhysHeap 的偏移，
但 helper 本身没有查询 Host BAR2，也没有做 Guest IPA→Host IPA 转换。

校验器还将 OSID 4 与 OSID 7 相对同一 MPC 首个 Guest 槽的偏移并列核对：Host 槽差值
和 PVR premap 地址差值都是 `3 * 8 MiB = 0x1800000`。这支持两个布局具有相同的
OSID 相对步进；由于它们的基址来自不同地址域，步进相同仍不能确定绝对基址、截距或
实际 backing。`python3 scripts/verify-fw-heap-slot.py --guest-osid 7 --mpc-card-base 0x3e800000 --no-upload-check`
会输出这三项相对 delta，并继续将绝对转换标为未证明。

PVR 的 E1C `FW_PREMAPn` 设备地址、FW_MAIN `sCardBase` GPU PAddr、Host BAR2 offset
仍是三类地址。当前静态证据给出了 Host 按 OSID 计算 GPU PAddr 的公式和 Windows OSID 4
的参考锚点，但没有 OSID 7 Linux 运行态 PhysHeap PAddr，也没有证明当前 Guest 实际
收到并使用了该 Host 记录。校验器将三类候选并列输出，不把它们直接等同或用作
`ioremap` 地址；Guest E1C premap 的运行时 backing 和当前 OSID 7 映射仍未实证。
Windows `MmMapIoSpace` 路径也只证明本地 PCI 资源映射，不提供 Linux Guest PVZ
transport 或 Host receiver。

## Linux Host/Guest 信息页 ABI 边界

继续核对同一 Linux 2.3.0 Host core 的 `vgpu_access_pci_bar1_region`：它分配并构造
`0x8a0` 字节响应，写入 magic `0xaa557491`、version `1`、OSID 和旧版 `vgpu_info`
字段；固件堆 base/size 分别来自 Host 私有表记录并写到 `0x848/0x850`。这与配套
Guest 头文件中 `fw_heap_base/fw_heap_size` 位于 `0x848/0x850` 的布局相符。新的静态
校验器会检查 Host 侧 version/magic/固件字段写入，以及 Guest 侧消费路径。

Windows RM 保存的 version-2 信息页采用不同的紧凑段表布局（24 字节段项，段表从
`0x28` 开始），因此不能把 `reports/device-info.bin` 里的 Windows v2 字段直接解释为
Linux Guest 旧版结构的 `segment_info[VVPU]` 或 `fw_heap_size`。已保存的 Linux OSID 7
记录只有解析后的摘要，没有旧版页的原始 flag、`0x438` 字段和固件堆记录字节；当前
仍无法判定 Guest `fw_heap_card_base` 分支实际得到的值。Host 反汇编确认了格式写入，
但没有给出当前 VM 的完整运行态页内容或 Guest→BAR2 映射。

公开 `mtgpu_platform_data` 的 x86-64 字段顺序也已核算：`vz_data` 从 `0x38` 开始，
`fw_heap_card_base` 在嵌套结构内偏移 `0x18`，合计 `0x50`。校验器从配套头文件推导此
偏移，并确认 PVR `PHYS_HEAP_CONFIG.sCardBase` 在公开结构中的偏移 `0x20`。这闭合了
Guest 本地数据流的 ABI 偏移，不会闭合 PVR premap 与 Host BAR2 之间仍缺失的地址转换。

`python3 scripts/verify-fw-heap-slot.py` 默认报告 OSID 4 参考值；OSID 7 的地址域
并列计算命令仍为：

```sh
python3 scripts/verify-fw-heap-slot.py --guest-osid 7 \
  --mpc-card-base 0x3e800000 --no-upload-check
```

这仍是静态拓扑假设下的地址计算，不会读取 PCI 或写入设备。

## 结论

现在有强静态证据表明，MTT Host 为每个 OSID 预留了与 Windows Guest 固件段相符的
8 MiB BAR2 槽，并有 PVR 预映射 heap 的配置线索。这收敛了下一步适配点：Guest 必须
使用 Host 指定的 OSID 槽，而不是任意从 LMA heap 分配地址；同时仍须确认 MTT Host
是否把固定槽视作完整注册，或需要单独的 Guest PVZ provider。当前 Host 基址和 Guest
运行时 PhysHeap 地址均无独立实测，因此本轮没有把 stub 改为成功，也没有改动设备状态。
