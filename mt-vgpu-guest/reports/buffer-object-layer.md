# 显存对象与引用生命周期

2026-09-28。继续适配内存路径，未加载新模块、分配实际新显存或尝试恢复。

## 新增实现

`kernel/mt_bo.h` 提供独立于用户态 ABI 的内存对象核心，可由后续 GEM 对象嵌入。
它管理页对齐大小、后备内存、普通引用、CPU 映射引用和 GPU 使用引用；所有操作必须
由同一会话锁串行化。GPU 引用只是防止释放，不会建立 GPU VA，也不会提交工作。

`kernel/mt_bo_vram.h` 将该核心接到已实现的 Guest 显存分配器：

- 仅从 `MT_POOL_NORMAL` 分配，继续使用已解析的 BAR2 偏移和 GPU PA。没有把
  Windows 指针、Host PA 或 Linux IOVA 混为一类地址。
- 创建前检查长度/对齐/40 位设备地址，成功分配后清零整个页对齐区域，包括尾部。
  分配、后备信息校验或清零失败均不发布对象，已取得区域会归还。
- CPU 映射保留对象引用；映射 cookie 仍是 I/O 内存，后端读写通过
  `memcpy_fromio`/`memcpy_toio`，不是普通 RAM 指针或用户态 mmap 地址。
- CPU 使用与 GPU 使用互斥；当前 GPU 使用为独占，等待后续调度器实现读写 fence
  排序。调用者必须在真正完成或取消硬件使用后才能归还 GPU 引用。
- 最后一个普通引用释放时，尚存的映射或 GPU 引用继续保留对象。后备释放、计数
  扣减和模块引用归还仅发生一次，不允许普通 put 消耗活动使用所持有的引用。

主模块的外层设备对象新增内存对象管理器，共用 `trial_lock` 和现有普通显存池；
分配后持有模块引用，移除路径检查活跃对象。`struct mt_guest` 原布局保持不变。
新增只读 `buffers` 统计对象数和占用字节数。当前尚无对象创建 ioctl，默认仅初始化
管理器，不分配 BO，也不创建 render 节点。分配/清零/映射/释放后端已编入模块。

参考了本地 Native 驱动 `src/mtgpu-2.7.1-6.12/src/mtgpu/mtgpu_drm_gem.c` 的
GEM/显存分层，但没有移植其闭源 `mtgpu_vram_*` 实现或假定 Native UAPI 适用于当前
Guest；实际后备由本项目已还原的显存布局和分配器提供。

## 验证

`tests/bo_lifetime_test.c` 使用可注入失败的 RAM 后端执行实际对象核心，覆盖：

- 无效大小和对齐、地址/范围溢出、分配失败、错误后备信息、清零失败、映射失败；
- 页尾清零、两个 CPU 映射持有期间关闭对象、GPU 使用期间关闭对象；
- CPU/GPU 冲突、重复结束/释放、引用计数溢出；
- 10,000 次串行交错操作，与独立的所有者/CPU/GPU 引用计数模型比较，最终分配与
  释放、映射与解除映射均平衡。

AddressSanitizer、UBSan 和 Werror 测试通过。运行上下文与试验生命周期回归通过，
W=1 内核构建无警告，pahole 比较确认共享 mt_guest ABI 不变。

```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest
python3 scripts/verify-runtime-integration.py
```

输出与最新模块散列见 [runtime-integration-build.json](runtime-integration-build.json)。
RAM 后端验证不覆盖真实 PCI I/O 排序、Linux 锁竞争或硬件任务完成。

## 后续接入边界

这轮完成 BO 后备与引用层，尚未提供 GEM handles、用户态 mmap、GPU VA 绑定、
dma-buf 导出或 fence/任务提交。尤其不能把 BAR 私有内存直接交给要求普通系统页的
通用 sg-table 导出函数。下一步应接内存对象的 GPU 地址映射与 GEM 生命周期，随后
用真实任务及完成事件验证；当前没有新增硬件加速能力。
