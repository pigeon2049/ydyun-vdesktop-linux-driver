# Windows RM heap profile 与 PCI 资源映射审计（2026-09-29）

输入为本地 mtkm64.sys（SHA-256 0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33）的 Ghidra 导出。反编译函数索引与汇编保存在 [decompiled/mtkm64.sys](../decompiled/mtkm64.sys/)；只读 xref 脚本为 [TraceDriverReferences.java](../scripts/TraceDriverReferences.java)，运行结果保存在 [windows-device-xrefs.log](windows-device-xrefs.log)。

## Windows GPU 资源对象的堆数量

.data 中的对象描述符从 0x141107d88 开始：首字段指向字符串 gpu_device，+8 类型 ID 为 1，+12 尺寸为 0x168，+16 构造函数指针为 FUN_14003c4d0（0x14003c4d0），+24 析构函数指针为 FUN_14003c64c。FUN_14003a500 遍历对象描述符指针表，并比较描述符 +8 的类型 ID；Ghidra xref 将构造函数指针槽 0x141107d98 指向 FUN_14003c4d0。

构造函数读取初始化描述符首个 DWORD，并据此设置对象 +0x24 的 heap/segment 数：输入 selector 0x100、0x200、0x300 时为 4；0x400 时为 5。通过 [verify-windows-gpu-device-heap-count.py](../scripts/verify-windows-gpu-device-heap-count.py) 在 Unicorn 中仅执行 FUN_14003c4d0 的 0x14003c4d0–0x14003c64b 指令；两个后续初始化 helper 被替换为成功返回桩，设备、Windows 内核和固件代码均未执行。四组 selector 的 heap 数量分支均通过，结果见 [windows-gpu-device-heap-count.json](windows-gpu-device-heap-count.json)。

Linux 源码将 S3000 0x0222 的 PCI device ID 掩码到 family 0x0200，并选择 quyuan1_drvdata。Windows 构造函数的 0x0200 selector 也选择 4 个 heap，这为此前 Linux Local Guest 的“四个描述符、核心期望五个”提供了一条独立的跨实现佐证。尚未证明 Windows 构造函数 selector 与 Linux GET_DEVICE_ID() family mask 是同一字段或 ABI，所以这只提高 SysDevInit heap-count 候选的可信度，不能替代 Guest 核心的运行验证。

另一份 Windows 内核驱动 mtdispkm64.sys（SHA-256 e061a9bda23fe8ae0cee81522a8b89455bfddd0b295bc86704009fcdeb82a686）的 FUN_140017650 直接从设备信息的 +2 读取 PCI device ID，执行 0xff00 family mask，并将 0x0200 分支交给 FUN_140018100。这确认该 Windows 栈确实把 0x0200 用作 PCI 家族 selector。它仍不能证明此字段原样传给 mtkm64 的 FUN_14003c4d0，但让“该构造函数 0x0200 对应 QUYUAN1 类”成为更强的跨组件推断。

### 显示驱动 selector 的职责边界

复核 mtdispkm64 的完整反编译函数后，`FUN_140017650` 的设备 ID 来自 `param_2+2`，只按 `0xff00` 分派；`0x0200` 分支调用 `FUN_140018100`。后者设置对象内两个计数（`+0x1e88 = 4`、`+0x1e8c = 8`），从静态表装入配置指针，并按设备信息的标志位筛选条目。返回后，调用者按这些计数遍历并构造后续节点/资源记录。

这条路径没有 `MmMapIoSpace`、PVZ map/unmap 回调或 Host 地址转换。按其数据流，它更像显示/引擎节点的家族配置，而不是物理堆映射；`4` 不能被当成 `mtkm64.sys` 物理堆数的独立证据。跨组件旁证仅限于“Windows 显示栈也用 PCI ID 高字节 `0x02` 作为 QUYUAN1 家族 selector”。Guest 四堆修正仍应依据 `mtkm64.sys` 的 `FUN_14003c4d0`、Linux S3000 family 选择和 `PhysHeapsInit` 失败计数，不应从这个显示配置 helper 推导。

### Windows Guest 的 PCI 识别与启动门

`mtkm64.sys` 的 `FUN_140003af4` 由 `StartAdapter`（`FUN_1411c62b8`）调用。它通过对象内的读取回调先请求 PCI 配置空间 4 KiB；若长度或厂商检查失败，再回退到 256 字节。函数要求 Vendor ID 为 `0x1ed5`，沿配置头 `0x34` 的 capability 链查找 ID `0xaa`，并检查其后 16 位值为 `0xaaaa`。未匹配时仅记录 “VGPU Env is abnormal” 并把该适配器标记为非目标 vGPU 环境。

确认目标环境后，函数通过另一个对象回调把 PCI Command 字（配置偏移 `4`）的 bits `0x6` 写回，即启用 Memory Space 与 Bus Master；这一过程与 [PCI enable 审计](pci-master-audit.md)的 Windows 参考路径相符。这里证明的是 WDDM/内核回调可访问虚拟 PCI 配置空间及设备 capability，不是固件堆映射：函数没有传递 8 MiB FW_MAIN 的 PAddr、Guest IPA/Host IPA 对，也没有调用 Linux PVR PVZ 的 map/unmap ABI。该 Windows 发现/使能入口不能填补当前 Guest 的 Host backing 证据缺口。

## Windows 的内存映射并非 PVZ provider

- FUN_14003d038（0x14003d038）处理资源类型 0x1040001，把 segment 描述符交给 FUN_14003eadc 以登记到本地 gpu phys heap resource allocator。
- FUN_14003ca64（0x14003ca64）从该 allocator 取得地址区间，再以设备资源基址加偏移调用 MmMapIoSpace；失败/释放路径回收本地映射和 allocator 项。
- FUN_14003abe4（0x14003abe4）区分 system address space 与 PCI address space。PCI 分支调用 MmMapIoSpace 映射设备基址、heap 基址和请求偏移的和；system 分支构造页数组并走系统内存映射 helper。

这些路径是 Windows RM 进程/驱动里的本地 BAR aperture 与资源描述符映射。它们没有呈现 Linux VMM_PVZ_CONNECTION 的 pfnMapDevPhysHeap / pfnUnmapDevPhysHeap、Guest IPA→Host IPA 转换参数或 Host PVZ 接收端，因此不能直接移植为那两个 Guest 回调。可见 BAR2 地址也不能单独证明 Host 固件 heap 已登记。

复核使用 verify-windows-gpu-device-heap-count.py、corpus.py function mtkm64.sys 14003c4d0、14003ca64 和 14003d038。
