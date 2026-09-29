# 三个独立动态池接入 Guest BO/VM

本轮将上一阶段确认的三个动态池接入主模块的独立分配、借用 BO、原子 VM 映射及
退出清理。所有验证仍为原始指令模拟或内核 RAM 测试；没有操作 Host、执行恢复、
加载主模块或提交 GPU 工作。硬件加速尚未启用。

后续已补齐[上下文池内切片与保留区TQX上传](context-pool-slices.md)，以下保留本阶段记录。

## 来源与映射

`01ccd8` 建立 Guest 保留区后，`01dafc/01d98c/01d3c8` 分配三个独立后备，
放入适配器 `+0x1060` 指向的22项管理器。`014d58` 按 PB、动态池、固定资源的
顺序枚举，最终通过 `00a630/009258` 逐页映射，PTE输入 flags 为0。

| 堆 | VA | 后备大小 |
| --- | --- | --- |
| 1 | `0x81ffd03000` | 2 MiB |
| 2 | `0x84fff00000` | 1 MiB |
| 10 | `0xf0ffe00000` | 2 MiB |

`process_resources_reference.py` 现直接执行池创建，随后执行静态资源与 Paging
Command 创建，再枚举真实22项池管理器。静态 USC 描述符与动态 USC 池描述符的
独立性有显式断言。此前人工放入静态 USC 描述符的列表夹具已移除。

16组组合覆盖有/无动态池、两条分页分配路径、PB/Fence可选组合，原始逐页调用共
38,920次；Linux生成的完整映射序列全部对照通过。物理页列表、OS分配和最终页面
写入边界仍采用模型；GPU没有参与此测试。结果见 `process-resources-validation.json`。

## Linux 生命周期

- `mt_reserved_pools.h` 持有三份独立 VRAM 分配，失败回滚；所有者放在外层
  `mt_guest_device`，不改变与保留模块共享的 `mt_guest` ABI。
- `mt_boot_bo_store` 缓存从六项扩为九项。所有 VM 复用同一 BO 引用和 CPU/GPU
  排他状态，动态池不借用静态 USC 缓冲区。末次引用销毁才清空缓存。
- `mt_process_resources_bind_pools` 将固定组与动态池作为一个批次绑定。任一引用、
  范围或页表容量检查失败，都不提交部分映射。生产 `bind_boot_shared` 使用完整组。
- 退出及既有试验改写准入检查现覆盖所有九个缓存槽位，借用对象尚在使用时不能
  释放其后备。分配/借用不清零、不上传，不将准备状态当作可执行状态。

TQX的九个任务对象与九个共享对象合计18项映射，原先16项上限不足；现统一增加到
24项，任务引用数组随同扩展。组合测试需要 **21页页表**，包括根页、8页PD、12页PT。
此前按20页估计的测试失败后，已用地址覆盖独立计算确认21页，并增加20页不足时
不留下共享映射的检查；32页后备下成功。

## 验证

- 三处动态后备分配失败回滚、18处借用视图分配失败。
- 九个缓存引用获取位置和九个批量绑定引用位置分别注入溢出；两阶段均完整回滚。
- 八个 VRAM 后备节点逐一脱离所属链表时拒绝借用；系统RAM末页地址错误仍拒绝。
- 两个VM复用九项缓存；关闭两个VM后，动态USC池仍由任务引用持有，最后释放才清空。
- 15组集成测试、ASan/UBSan、W=1主模块编译及共享ABI检查通过。
- 真实内核 GEM/RAM **257项检查**通过：21次普通分配/释放、12份启动后备分配/释放；
  对三个池的全部1,280页及系统RAM的1,024页执行独立页表遍历。
- TQX准备持有 **19个BO**（18个对象及页表），共享对象均取得任务使用引用。
- 真实内核 RAM fence **321项检查、2次回调**通过。MMU回归38组稀疏树/129页、
  3组dummy和14组无效输入通过。

所有测试模块已卸载，测试后 PCI 仍未绑定，主实验模块未加载，仅有 QXL card0。
记录为 `runtime-integration-build.json`、`gem-kernel-validation.json`、
`fence-kernel-validation.json`。本轮主模块SHA256：
`290ca968a5e15821b4d841cba116a95cfc511728418548a2f71d5b766240dc03`。

## 尚需完成

动态池的按上下文子分配、程序数据进入池的原始路径、上传读回与上下文发布仍待接通。
现有TQX测试可用独立任务BO构造命令，但不能据此认为生产池内布局与真实提交已经完成。
静态PDS/USC新镜像仍在CPU暂存中；本轮没有扩大硬件试验上传列表，也没有开放DRM节点。

复现：

```sh
python3 scripts/verify-process-resources.py
python3 scripts/verify-runtime-integration.py
python3 scripts/verify-mmu-bootstrap.py
python3 scripts/verify-gem-kernel.py --kind gem --run
python3 scripts/verify-gem-kernel.py --kind fence --run
```
