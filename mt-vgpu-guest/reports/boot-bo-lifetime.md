# 启动缓冲区到 BO/VM 的生命周期桥接

已将实际 `mt_boot_resources.blocks` 及独立Paging Command接入 BO 引用和 VM 共享映射接口。
后续纠正了末项资源身份及内存类型，详见 [4 MiB大小/VA](paging-command-correction.md)与
[系统RAM后备](system-memory-path.md)。
主模块在准备启动资源后安装管理器，VM 可调用 `bind_boot_shared` 取得现有缓冲区
的引用视图。验证使用 RAM；生产主模块未加载，没有 GPU 发布、恢复或 Host 操作。

## 所有权设计

`mt_bo_vram_handle` 区分由 BO 分配的显存与借用的启动分配：

- 普通 BO 继续由后端分配、清零并在最终释放时回收。
- 借用 BO 必须来自同一 `mt_vram` 的有效分配链表，检查映射、大小、页对齐及
  40位物理地址边界。创建视图不分配显存，不改内容，也不移动原链表节点。
- 借用视图拒绝通用 `clear` 操作。最终 BO 释放只销毁视图，不 unmap/free 原分配。
  显式 BO 读写接口继续遵守 CPU/GPU 排他；导入并不授予绕过排他的能力。
- 每个存活视图持有模块引用，并计入 BO 管理器对象数；因此它可以在 VM 关闭后
  继续被 GPU 引用持有。原启动对象和管理器必须活到所有视图释放之后。

`mt_boot_bo_store` 的六个槽位保存非持有式缓存指针。首次绑定按需创建视图；其他 VM
复用同一个 BO，避免同一块物理内存被两份独立占用计数管理。所有操作持有共同会话锁。
整组 VM 绑定后释放临时引用，只保留 VM/CPU/GPU 实际持有的引用。最后一个引用
释放时，容器析构函数清空缓存槽。空闲管理器本身没有永久模块引用，允许正常卸载。

绑定失败时，已创建的临时视图全部回收；已有缓存对象的引用恢复原值。复用前一轮
原子映射逻辑，VM 不会留下部分共享资源。

## 主模块接入与退出

`mt_guest_device` 的外层对象新增管理器，保留共享 `struct mt_guest` ABI。
`prepare_resources` 成功后将管理器连接到地址空间管理器；管理器仅安装指针，不自动导入、
绑定或写入资源。`bind_boot_shared` 使用五个启动块及单独分配的4 MiB Paging Command，按固定VA映射。

模块移除检查未释放视图，既有 BO 对象检查和模块引用也会阻止提前释放。
`trial_control` 在断开/恢复之前增加占用检查，因为试验恢复会改写共享的 YUV/Kill
等启动分配；即使 GPU 当前空闲，只要视图仍由 VM 持有就返回 `-EBUSY`。
这只是新增代码防护，**没有执行试验恢复**。运行上下文已发布时的原有限制继续生效。

BO统计中的 `allocated_bytes` 包含当前视图所覆盖的字节，不表示借用操作又分配了
同样大小的物理显存。五个原启动分配仍归 `mt_boot_resources` 管理；Paging Command系统RAM由外层对象持有，
通过独立的`mt_system_memory_fini`释放。

## 验证（初次桥接记录，最新结果见纠正报告）

新增 `boot_bo_lifetime_test.c` 执行真实 BO 后端、视图缓存及 VM 操作，用 RAM 模拟
显存分配、I/O和 OS 原语。覆盖：

- 12个容器/后端句柄分配位置逐一失败，全部撤销，无模块、堆或映射引用泄漏。
- 外部块以及六个脱离所属分配链表的块拒绝，先前临时导入全部回滚。
- 两个 VM 复用同一组六对象；已有对象引用溢出、重复映射后状态保持。
- VM 关闭后 GPU 引用继续持有对象，CPU访问拒绝；最终引用释放才清空缓存。
- 所有视图消失后重新创建；原六块内容逐字节保留，借用路径无清零或提前释放。
- 普通 BO 分配与销毁同时回归，模块引用及所有后备分配释放平衡。

`verify-runtime-integration.py` 现有 **13组测试通过**，ASan/UBSan、W=1构建、
共享 ABI 对照通过。主模块 SHA256：
`41e7509502cc219bb3698cecedcedd5aa1f5a626edf91bb601d2a2dd42eca993`。

真实内核 `mt_gem_selftest` 新增 RAM 启动分配链表及实际借用后端测试，
**222项检查通过**。既有后备18次分配/18次释放，新借用测试后备8次分配/8次释放。
后者含六个共享资源和两个页表块；测试从未调用 PCI 映射或显存池分配。
模块散列为 `e224f03310c77013b5bb59c502e810ede73899ac6dbdfd3ea368fbf3e5e782d4`，
临时模块已卸载，前后 PCI 未绑定、主模块未加载，只有 QXL card0。

复现命令：`python3 scripts/verify-runtime-integration.py` 和
`python3 scripts/verify-gem-kernel.py --kind gem --run`。
机器可读结果为 `runtime-integration-build.json` 和 `gem-kernel-validation.json`。

## 剩余工作

引用视图不证明共享资源已经初始化或上传。当前启动试验仅处理部分资源的上传，
Static PDS、Fence、Paging Command 等的完整内容及准备状态仍需继续核实和接入。
动态 USC/命令池、运行上下文发布、已发布VM上的后续任务及安全撤回也未完成。
实际 `bind_boot_shared` 尚未对真实 BAR 后备执行，工作使能仍关闭，未获得硬件加速。
