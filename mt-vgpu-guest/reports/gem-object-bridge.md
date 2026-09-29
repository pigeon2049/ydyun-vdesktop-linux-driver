# Linux GEM 内部对象桥接

2026-09-28。本轮继续适配，不尝试恢复。主实验模块只编译；另外加载并卸载了
一个完全使用普通 RAM 的内核自测模块，没有绑定 PCI 或提交 GPU 工作。

## 实现与生命周期

`kernel/mt_gem.h` 提供真实 Linux DRM GEM 接口的内部封装：私有后备对象初始化、
文件句柄创建、文件私有句柄查询和将其 BO 绑定到已创建的 GPU VM。调用者提供有效的
DRIVER_GEM 设备，设备父对象必须与此显存管理器一致。它不创建 DRM 设备或注册节点，
尚未作为 ioctl 暴露；主模块仅初始化管理器和函数表。

依据本机 Linux 6.12 头文件与 [Linux v6.12 GEM 核心源码](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/drm_gem.c)，
使用 private_object_init 表达由驱动管理的显存后备。每个包装显式持有 drm_device
引用，释放对象内部资源后归还。lookup 返回的临时 GEM 引用保住 BO，直到 VM 成功
取得自身引用；GEM put 均放在会话锁之外，避免 free 回调再次取锁时死锁。

原 BO 只持有显存引用，元数据容器仍归创建者所有。新增可选的最终 release 回调，
最后一个引用退出时先释放后备、清零 BO，再调用容器析构函数；析构之后不再访问 BO。
GEM 使用独立分配的 BO 容器。因此文件句柄和 GEM 包装都可以先退出，而 VM、CPU 或
GPU 使用引用继续保住元数据与显存，最后由结束使用的一方完成销毁。

分配、清零、句柄创建失败均清理已取得资源，输出仅在成功时更新。拒绝不匹配的设备、
显存管理器及其他类型的 GEM 对象。没有实现私有 BAR 显存到 dma-buf 的正确导出路径，
也没有用户映射撤销和 fence 排序，因此显式拒绝 export/mmap，避免走系统内存的
默认导出路径。没有声明兼容旧 Native 用户 ABI。

buffers sysfs 新增 gem_objects 和 drm_registered=0。当前只初始化管理器，不创建
真实 GEM/BO；新模块已有 DRM 符号依赖，后续加载需要 DRM 核心。原 mt_guest 结构 ABI
仍与保留的旧模块备份一致。

## 两层验证

`tests/gem_lifetime_test.c` 执行实际 GEM 桥接、BO 和 VM C 代码，DRM/锁/显存后端
使用模型。验证内存分配和清零失败、句柄创建失败、文件私有句柄隔离、错误管理器、
绑定失败回滚、查询后关闭句柄的交错，以及 GEM→VM→CPU/GPU 的依次退出。
ASan/UBSan/Werror 通过，所有容器、后备和模拟 DRM 引用最终平衡。
这不是对真实 Linux 文件句柄实现或并发调度的测试。

`kernel/selftest/mt_gem_selftest.c` 使用真实内核 drm_dev_alloc 和 GEM 引用回调，
临时父设备只有 sysfs 生命周期，DRM 设备不注册，不产生 card/render 节点。
后备内存全部使用 kvzalloc；GPU PA 仅作为软件页表测试值，没有 MMIO、固件或任务。
实际 GEM 包装释放后两份 VM 绑定继续保住 BO，销毁 VM 后 GPU 使用引用继续持有，
最后结束使用释放 BO。最新增加工作包暂存检查后共 18 项通过，RAM 分配/释放为 2/2，内核没有新增
WARNING/BUG/Oops，临时父设备已删除、自测模块已卸载，原 DRM 节点集合不变。
结果见 [gem-kernel-validation.json](gem-kernel-validation.json)。

后续已添加 prepare_work 内部接口，检查句柄 BO 在目标 VM 的命令范围后构造 CPU
暂存包；不是提交 ioctl。字段来源和新测试见 [工作包适配](work-command-path.md)。

```sh
python3 scripts/verify-runtime-integration.py
# 只编译自测模块；不加载：
python3 scripts/verify-gem-kernel.py
# 明确加载 RAM 自测模块、检查并卸载：
python3 scripts/verify-gem-kernel.py --run
```

主模块集成回归、W=1 编译和共享 ABI 核查也通过，见
[runtime-integration-build.json](runtime-integration-build.json)。
真实内核自测仍不覆盖实际 DRM 文件句柄、用户 mmap、PCI I/O 或 GPU 完成事件。

## 最新现场与下一阶段

本轮检查发现旧的“通信恢复模块仍在运行”描述已经失效：当前启动 ID 是
`32f721d8-4532-4f9e-ba54-baf34e126e09`，主模块未加载，PCI 设备未绑定。
只读 preflight 返回 Guest2/FW1，拒绝新的干净连接试验；没有执行试验或恢复。
记录见 [gem-stage-preflight.json](gem-stage-preflight.json)。

还需要接通 DRM 设备/文件生命周期与 ioctl、fence 和命令提交、完成事件分发，以及
匹配的用户态访问路径。当前完成的是内核内部 GEM 桥接，未新增硬件加速能力。
