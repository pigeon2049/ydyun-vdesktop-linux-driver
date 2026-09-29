# 上下文池内切片与保留区 TQX 上传

接入上下文持有的池内切片，并让TQX编码/上传使用动态池及共享静态PDS。
280项真实内核RAM检查通过；未向真实设备上传或执行GPU工作，尚无硬件加速。
后续加入私有GEM自动准备入口和按上下文复用池切片的路径；本轮只做W=1编译，
没有重跑测试或加载模块。没有恢复或Host操作。

## 原始回调

`0219d0 → 041cc4 → 01dbdc` 按4 KiB舍入分配，`019b80/019a7c` 将记录加入
上下文列表。列表满或记录分配失败时，`01dc9c` 归还池内空间。

| 类型 | 来源 | GPU地址 |
| --- | --- | --- |
| 3 | 堆10，纹理状态池 | `0xf0ffe00000 + offset` |
| 4 | 堆1，PDS状态池 | `0x81ffd03000 + offset` |
| 6 | 堆2，USC池 | `0x84fff00000 + offset` |
| 5 | 既有静态PDS描述符 | `0x81ffc00000` |

`verify-context-pools.py` 执行真实回调、范围分配器和列表插入，15种请求、5次静态PDS
获取、9种分配/列表失败回滚通过。OS后备、元数据分配和锁采用模型；完整SDK上下文
创建没有在此执行。详见 `context-pools-validation.json`。

## SDK堆表纠正

此前 `verify-tqx-heaps.py` 将 plan `+0x4f8` 的普通可分配堆表传给SDK，这份表
排除了内部保留区，因而对池地址触发原始断言。`01a558` 实际将完整plan指针存入
platform `+0x10`，`042000` 返回此指针，随后 `093494` 安装完整堆范围。

验证器现执行 `042000` 并使用返回的描述符指针，保留原始断言；旧表仍用于普通
分配边界。Linux允许普通区域、三个已证实的池和静态PDS，继续拒绝其他保留区和
跨区/越界请求。180组完整命令流及9页镜像对照通过，其中36组使用保留池和静态PDS；
另有276组相对地址、66组类型选择、26组无效放置及5组普通堆边界检查。
页内状态打包采用Linux布局，不是完整Windows分配策略复刻。

## Linux接入

`mt_pool_slice.h` 用位图管理连续页，切片持有池BO和上下文计数。
`mt_boot_pool_alloc` 将类型3/4/6路由到设备共享的池状态，只接受已映射到当前VM
的对应BO。调用者提供已选定的请求大小，本接口不代替完整SDK的大小选择策略。

Linux选择第一个足够大的连续空闲区；Windows使用按大小分组的分配器，因此碎片化
后的具体偏移可以不同。编码器使用返回的实际VA，不假设固定偏移。切片对象必须
保持地址稳定，复制的记录不能释放真实分配。

## 私有 TQX 准备入口

`mt_tqx_work_prepare_from_pools`现由内部GEM桥接调用：调用者提供command、source、
destination、DMA和engine-state五个普通BO；驱动绑定静态PDS与共享资源，按类型6/4/3
首次为上下文分配shader、PDS状态和纹理切片，再调用完整TQX编码、BO写入/读回、页表上传及任务pin。
切片记录由上下文保存，工作对象和pending fence只借用指针；匹配fence完成后GPU pin释放，下一任务
可继续使用相同切片/VA。已封存且已上传的VM只写这些既有映射中的BO内容，不再重写页表。
新入口先拒绝不支持的route/profile、跨store对象和超过单页的状态/纹理需求，避免在明显无效请求上
留下映射或分配。`release_tqx_context_pools`只在上下文任务、VM使用和相关BO CPU/GPU使用都归零时释放缓存。

这个入口仍是私有内核函数：没有公开ioctl/DRM节点，也没有接入一个实际运行的Guest上下文；
marker的实时`work_ready`门仍关闭，因此它没有令硬件开始执行。

上下文切片在其生命周期内只分配一次；任务匹配完成释放GPU pin后允许在同一固定映射内更新BO内容，
不解除或重用页表中的VA。Windows `DestroyContext`（DDI表中的 `140004200`）调用
`SIMDestroyContext`（`14000f328`），销毁上下文关联的动态资源；反编译中没有发现它清零或撤销
进程VM根的操作。Linux的池切片是已映射池BO内的位图子分配，释放仅归还位图区间和切片BO引用，
不解绑/改写页表，因此现在允许在已上传的封存VM上释放或新租用切片，但要求VM无活跃使用且相应BO
无CPU/GPU使用。新增 `destroy_execution_context` 将切片回收与软件上下文销毁串联；持有TQX缓存的调用方应走此GEM入口，
不能直接调用基础结构的 `mt_execution_context_destroy`。它不撤销VM根。
根移动通知、根撤回和VM最终销毁仍未接通。并发提交暂时由上下文活跃计数串行化。
当前仍以整个BO作为排他单位，同池不同切片的并行CPU更新受到限制，尚未实现区间级同步。

## 验证与限制

- 三池舍入、填满、耗尽、释放、整池复用；BO引用和上下文计数溢出保持原状态。
- 拒绝复制记录释放、重复释放、带切片销毁上下文及任务使用期间释放。
- 内核RAM测试先在每池占用一页，再分配USC、PDS状态和纹理切片，验证非零BO偏移
  下的完整TQX上传、读回、任务持有及取消；PDS程序使用共享静态PDS BO。
- 内核GEM/RAM280项检查，21次普通分配/释放、12份启动后备分配/释放；
  fence回归321项、2次回调通过，测试模块全部卸载。
- 15组集成检查、ASan/UBSan、W=1和共享 `mt_guest` ABI通过。

证据见 `context-pools-validation.json`、`tqx-heap-validation.json`、
`runtime-integration-build.json`、`gem-kernel-validation.json` 和 `fence-kernel-validation.json`。
原始池切片验证轮主模块SHA256为 `287909afa477c780cece145c8a847245669da8259f7b508877a80067ba984974`；
上下文缓存/封存根复用版本的SHA256为
`5c30c8f7b0bc089ea1f65c993c847bf6f03cb2166e29c6b043eb717223c89821`。加入封存VM空闲切片回收和
上下文销毁串联入口并移除“封存VM中新上下文”重复拒绝条件后，经W=1编译的模块SHA256为
`6c5d22303d29b2a470819281f7c614d3860dec2fce31700774c2de2a75054fb6`。
再加入 pending VM use 阻止进程对象销毁后，当前W=1构建的模块SHA256为
`5b86c76562265b0d33ab18061c9b08441517942840ff40769384149456f4338c`。

生产侧已有自动分配、按上下文复用、空闲封存VM切片回收和私有TQX上传/任务持有串联，但完整上下文构造、公开用户接口、
根移动/最终撤销通知、跨上下文并行和真实设备上传/执行仍需完成。RAM验证和W=1编译不证明GPU已执行。

复现：`verify-context-pools.py`、`verify-tqx-heaps.py`、`verify-runtime-integration.py`，
以及 `verify-gem-kernel.py --kind gem --run` / `--kind fence --run`，均位于 `scripts/`。
