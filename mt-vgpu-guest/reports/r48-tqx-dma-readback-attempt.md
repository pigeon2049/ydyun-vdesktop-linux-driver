# r48：TQX 从 DMA-backed system page 回读尝试

在 r47 retained session 上执行一次性 256-byte TQX copy。DMA source BO 使用
`dma_map_page()` 返回的 IOVA 作为 GPU 页表 leaf，目标是普通 VRAM BO。

## 结果

- 只读 marker 先完成：DM1 `sequence=1 result=0`。
- DMA-source VM 准备成功：root `0x78300d000`，15 个页表页、14 个 mapping；
  source IOVA `0x172149000`，目标 VRAM BAR2 offset `0x122e000`。
- 唯一 TQX job 已提交并收到完成 fence：`sequence=2`，随后 `pending=0,
  completed=2`，Guest/FW 保持 `2/2`，`event_result=0`。
- 内容核对失败，`result=-84` (`-EILSEQ`)，`verified=0`。另一个只读 helper
  从主驱动已有的 BAR mapping 读取目标页，发现 offset `0x122e000` 的首字为
  `0x00000000`，而预期为 `0xeea55c13`；因此目前**不能**认定 GPU 成功读到了
  DMA source 内容。未再提交第二个 job。
- 没有 WARN/BUG/Oops；当前 GPU 队列空闲，但实验 VM 已 sealed 并保留。

## 当前保留状态与下一步

`mt_live_tqx` 和 `mt_guest_probe` 都被引用保持加载，不能热卸载或解绑。该实验版
源页分配使用 `kvzalloc(4096)`；它是否 page-aligned 尚未在本轮记录，存在一个需
排除的假设：若 CPU 地址带页内偏移，`dma_map_page(page, 0, PAGE_SIZE, ...)`
映射的是页首，而非 pattern 起始位置。源码已改为 `vzalloc()` 并显式断言
page-aligned；新增运行后诊断参数分别报告 destination/source mismatch 和首字。

该假设尚未证实，destination 为零也可能来自 PTE/descriptor/device-address
解释差异。验证修订版前，必须重启以释放 pinned session；第一次失败后没有继续
触碰 GPU，也没有卸载任何 pinned 模块。

第一次失败后、第二次重启前：loaded `mt_live_tqx` build-id 为
`09f4fbbbd59a772ae8c563be2ea669b6fe0b8f6f`；修订版 build-id
`703d55075cd9621dbae67836c32ae7253be260d5` 尚未加载。主模块引用数 17、实验
模块引用数 1，`pending=0/completed=2`，BusMaster 开启；不能热替换这些 pinned modules。

## 修订版复测（第二次重启后）

修订版使用 `vzalloc()`，日志确认 `cpu_page_offset=0`；CPU 页在 DMA sync
后首字仍为 `0xeea55c13`。唯一复测 job 仍以 sequence 2 正常完成，但目标首字
仍为 0，`destination_mismatch=0`。独立只读页表回读进一步确认：root、directory、
leaf 均 present，PTE `0x1d573f001` 的物理页地址恰为 DMA IOVA `0x1d573f000`。

因此 `kvzalloc` 页内偏移和页表 leaf 地址错误已排除；当前问题缩小为 DMA IOVA
在设备/Host 路径中的可达性、同步或 TQX source descriptor 解释差异。Fence 成功
只说明 job 收到完成事件，不证明数据复制正确。没有再提交 job；只读 helper 已
卸载，主会话及 sealed TQX VM 仍 pinned，pending=0、BusMaster 开启。下一步应先
静态核对 DMA 地址域/descriptor 与 Host vGPU 映射契约，再决定是否需要新的硬件窗口。

重启后再次只读解码信息页/窗口：OSID 1、flags `0x3d1`、window bias
`0x8000000000`、`info+0xca8` extra `0x800000000`，所以已有
`mt_system_page_address()` 路径给系统页使用的 GPU PA 域是
`Guest GPA + 0x8800000000`。此前实验 PTE leaf 使用 `dma_map_page()` 返回的
`0x1d573f000`；这与系统页转换契约不是同一地址域。下一版 TQX 验证已改为同时
保留 DMA API address（sync/unmap）并以 `page_to_phys()` 经窗口转换后的 GPU PA
构造 PTE。该修正尚未上机验证，不能据此宣称已解决回读失败。
