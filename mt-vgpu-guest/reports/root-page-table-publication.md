# TQX 根页表发布入口

本轮只分析本机已有的 `mtkm64.sys` 反编译库，没有访问宿主、加载模块或触发GPU命令。

## 原始调用路径

WDDM `SetRootPageTable` 回调位于 `140001d14`。它先检查适配器状态和对象签名；有有效
上下文对象时，调用 `14000a750` 将输入根地址转换后写入软件 Context+`0x30`，并同时写入
Process+`0xa8`。转换前的地址可带segment，`14000a600` 根据segment基址展开，再交给
`140021d90` 做平台地址转换。

随后 `140001d14` 调用 `140003498`。该函数先把转换后的 root 写入调用方提供的输出槽；
只有第三参数非空时，才从其接口对象内的函数表偏移 `+0x250` 取回调并传入 root。该间接
调用不是固件队列写入或GPU寄存器直写。后文追到，普通硬件节点上下文的第三参数为空，
所以不把这个可选回调臆造为 Linux MMIO 或 RPC 操作。

非空时，`140003498` 从第三个参数所指对象的 `+0x10` 接口表取 `+0x250` 函数指针，并以
该对象 `+0x200`、`+0x08` 和 root 作为参数调用；它把回调状态码返回给 `SetRootPageTable`。
在完整反编译文本中，这个 helper 仅由 `140001d14` 直接调用。替代上下文分支的接口注册/对端
仍未确认，不根据偏移臆测成某个 WDK 或 PCI 消息。

`SIMSubmitCommandVirtual`（`140014344`）随后会把Context+`0x30`作为提交包`+0x18`的
根字段。故“写出DMA描述符”和“让系统知道/接受此根”是两条不同的路径。适配器连接后
创建显示/内存窗口记录的 `14001ecdc → 1400231ec` 也不是这条 `SetRootPageTable` 回调。

微软公开的 [`DXGKDDI_SETROOTPAGETABLE`约定](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_setrootpagetable)
明确它用于根页表被移动或扩容时更新上下文，并保证目标上下文在更新期间处于idle；
[`DXGKARG_SETROOTPAGETABLE`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_setrootpagetable)
包含上下文句柄、GPU物理根地址和顶层页表项数。因而它不是每个DMA任务都重新发布根的接口。

## 普通硬件节点的条件回调

继续追 `CreateContext`（`1411c6a38`）后，`+0x250` 回调的适用范围缩小了。它先把一个
`0x28` 字节包装对象清零，再比较 `DXGKARG_CREATECONTEXT.NodeOrdinal` 与设备对象
`+0x248` 内的节点表数量（读取字段 `+0x2d0`）。微软 DDI 定义确认 `NodeOrdinal` 是
上下文所属的节点。数量内的节点走 `SIMCreateContext`（`1411ca690`），并把 SIM 上下文
放在包装对象 `+0x18`；包装对象 `+0x20` 保持为零。另一条路径走 `1411c49bc`，把替代
上下文放在 `+0x20`。

`SetRootPageTable` 将包装对象 `+0x20` 传给 `140003498`。该槽为空时，helper 仍先把
转换后的 root 写入调用方输出槽，然后因第三参数为空而跳过间接回调。输出槽地址来自
设备对象的 `+0x30` 子对象再偏移 `+0x20`；该槽的具体结构名尚未恢复。有效硬件节点的
`NodeOrdinal` 落在节点表范围内，因此走 SIM 上下文分支；TQX 工作提交使用的正是这类
硬件节点。普通 TQX 根更新会写入 Guest 软件 Context+`0x30`、Process+`0xa8` 和该设备侧
root 槽；`+0x250` 二级回调不构成它的 PCI/RPC 根发布命令。该二级回调仍只在替代上下文
分支适用，其具体接口未命名。字段条件来自原始分支和反编译；`NodeOrdinal` 的含义见微软
[`DXGKARG_CREATECONTEXT`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_createcontext)。

## Linux适配影响

Linux当前能构建VM页表、上传读回页表，并把GPU物理根地址编码进工作包。普通 TQX 节点
不需要模拟未知的 `+0x250` 二级回调；固定根已覆盖当前可构造的任务路径。既有任务不改VM
映射，因此适配代码在根已上传并封存、前序任务的匹配fence已释放所有GPU pin后，复用同一
上下文的池切片并只更新既有映射中的BO内容，不重写页表。切片按上下文持有，不按任务分配。

工作准备与发布是显式两阶段：`mt_tqx_work_prepare` 上传并读回初始页表，但不自行封存；调用方封存前，
`mt_marker_submit_tqx_work` 拒绝发布。新增长寿命 RAM 回归验证了这道门，并在首个模拟 fence 完成后再次
准备任务，确认仍使用同一 GPU 根地址且没有重写页表 BO。该检查只覆盖内存后备和软件队列，不验证设备端
根切换、TLB 或 GPU执行。

继续追踪 Windows `DestroyContext`（DDI入口 `140004200`）：普通上下文包装中的软件对象由
`SIMDestroyContext`（`14000f328`）销毁；该路径释放上下文动态资源并销毁辅助命令上下文，
没有发现对进程根地址或已发布根的清零调用。因此池资源回收和根撤回是两种不同的生命周期操作。
Linux现允许无活跃任务/VM使用时回收已封存根映射范围内的池子分配；回收只更新池位图和引用，
不修改页表。内部 `destroy_execution_context` 负责这项回收和软件上下文销毁，但不会销毁 VM。

随后追了 `DestroyProcess` DDI `1400063c0`：它先调用 `140002e28` 释放进程关联的注册资源，
再进入 `SIMDestroyProcess`（`140015178`）。该路径拆除进程保留的 GPU VA 区域
（`1400152e4 → 14000a8a4 → 1400195b8`），最后销毁其 MMU context（`140018650`）。
`1400195b8` 标记 MMU context 更新状态，并经后端表函数撤销叶映射及调整页引用；该路径
不同于根更新 DDI 对 `140003498` 的回调。
静态反编译中 `14000a750` 只有 `SetRootPageTable` 路径的直接调用点；`DestroyProcess` 中没发现
单独写零根或调用同一 WDDM 根发布回调的代码。它说明 Windows 把 VA 与页表资源的释放放在进程/MMU
销毁链中，但不能据此断言硬件 TLB/cache 的撤销语义。

Linux `mt_execution_process_destroy` 现在还会检查 VM `active_uses`，即使上下文计数为零，pending
fence 仍会阻止进程对象提前销毁。该检查只保护引用生命周期；封存 VM 根和映射仍须等独立撤销机制。

同一工作区里的 Linux Host 驱动反汇编提供了另一种生命周期样例：`mtgpu_vm_context_create_ioctl`
创建 `DevmemIntCtx`、heap 和映射树；销毁 ioctl 通过 staged handle release 进入
`mtgpu_vm_ctx_release`，逐项 `DevmemIntUnmapPMR` / `DevmemIntUnreserveRange`，最后销毁 heap 与
`DevmemIntCtx`。这是 Host DRM/PVRSRV 的内核上下文接口，不是 Windows Guest 的 `+0x250` 回调对端；
因此不能直接把 Host ioctl 或它的释放次序套进此 Guest VM。

普通节点的根值更新已经追到本地 MMU context 字段及后续任务包，但页表扩容时的 Linux 根替换路径
尚未接通。此分析也不能证明销毁时硬件 TLB/cache 不需单独撤销；封存 VM 的最终解绑/销毁继续拒绝。
这项复用只覆盖映射固定、页表已上传且没有活跃任务的路径，不等于已实现根撤销或并发提交。

后续应追踪普通节点根扩容时 Guest MMU context 的新根存储与工作包切换，并单独确认页表/TLB
撤销语义；替代上下文使用的 `+0x250` 回调不再作为普通 TQX 根发布的前置条件。实际工作负载仍未开放。
