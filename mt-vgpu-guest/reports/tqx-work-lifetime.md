# TQX 上传、任务引用与 fence 接入

已将九对象上传接入长期任务持有及真实 Linux `dma_fence` 队列。所有验证使用
CPU/RAM 后备或 RAM 消息环；没有向 GPU 提交工作，没有启用硬件加速。
本阶段不涉及恢复、重置或 Host 操作。

## 实现

`kernel/mt_tqx_work.h` 的 `mt_tqx_work_prepare` 在共享会话锁内完成：

1. 核对 family2/TQX、上下文、VM、九对象及页表占用状态。
2. 上传并读回此前的11页程序/DMA，保留独立引擎状态内容。
3. 上传并读回完整 VM 页表镜像，成功后设置 `vm->uploaded`。
4. 用 DMA 提交视图、上下文 token/PID 和实际根页表物理地址生成 type1/`0x67` 包。
5. 持有整个 VM 的去重 BO 集合，并增加 VM 和上下文活跃任务数。

测试 VM 持有九个资源对象及一个页表对象，共十个 BO。上传与取得 GPU 引用之间
不释放会话锁，因此不会留下可被 CPU 改写的间隙。GEM 新增 `prepare_tqx_work`
及 `cancel_tqx_work`；九个句柄的临时查找引用会被释放，任务的长期引用继续保留。
调用者必须保留 work/context 的存储，并遵循既有会话锁和对象生命周期约定。

`mt_marker_submit_tqx_work` 将准备好的任务所有权移入 pending fence，复用 DM1
和现有完成事件处理。不会再次增加上下文活跃数，也不会在交接时先释放再重新取得引用。
提交前仍要求运行准入、`work_ready`、已上传且封存的 VM。主模块没有打开
`work_ready`；只有 RAM 自测设置此标志。

队列已满、索引无效或准入拒绝时，准备好的任务保留，可重试或取消。
队列写入前失败会撤销 pending 项并恢复原始任务包；已分配的 fence ID 不复用。
成功后由 pending 项持有资源，即使外部 fence 和资源所有者均关闭也不会提前释放。
只有匹配 DM 与 wire ID 的普通完成事件才释放资源、减少上下文/VM活跃数，然后
触发 fence 回调。过期事件、重复完成及未支持的 HWR 事件不会当作成功完成。

## 验证

- `verify-runtime-integration.py`：12组测试通过，ASan/UBSan、W=1无警告构建通过；
  共享 `struct mt_guest` ABI 与保留模块一致，主模块未加载。
- `tqx_submission_test`：60种准备阶段 I/O/map 故障，覆盖11页资源和完整页表的
  写入、读取、损坏及映射失败；失败不取得任务所有权，成功取得十个 BO 引用。
  同时验证 CPU 排他、上下文忙销毁、取消和重复取消。
- `gem_lifetime_test`：九个句柄位置分别查找失败；全部句柄在查找后关闭时，仍能
  完成准备并由任务持有资源，最终取消和销毁后引用平衡。这里的 DRM 核心为模型。
- `verify-gem-kernel.py --kind fence --run`：**321项真实内核 RAM 检查通过**，
  总计2次 fence 回调。新增路径覆盖运行准入、VM封存、错误归属、无效队列、队列满
  和重试，以及外部所有者关闭、CPU排他、错误事件、匹配完成和十个 BO 最终回收。
  RAM消息包中实际核对了 opcode、根页表、DMA VA、4864字节长度、wire ID和PID。
- GEM公共上传辅助函数重构后，`verify-gem-kernel.py --kind gem --run` 的既有
  **138项内核RAM检查再次通过**，后备12次分配/12次释放。该组覆盖真实GEM对象
  回调及上传回归；新增准备入口由上述GEM模型和内核fence测试分别覆盖。

机器可读结果见 `runtime-integration-build.json`、`gem-kernel-validation.json`
与 `fence-kernel-validation.json`。
fence 测试时间为 `2026-09-28T06:02:35.994481+00:00`，测试模块 SHA256：
`045ad42a04f15e1341026ca4501e7c3c74ec93b51b497b63ef278713ea3d326a`。
测试模块已卸载；检查前后 PCI 未绑定、主模块未加载，只有 QXL card0。

## 剩余边界

这是内部首次提交路径，尚无 DRM 用户 ioctl、完整上下文发布或 GPU 执行验证。
上传仍拒绝已封存的 VM；不能据此声称已支持同一已发布地址空间上的连续工作。
完成或取消任务也不会自动撤销根页表发布或解除封存。RAM 自测的解除封存仅用于
清理测试对象，不能移植为实机退出流程。

I/O 失败可能留下部分新 BO 内容，必须完整重试或丢弃；页表读回失败保持
`uploaded=false`。新状态 BO 的硬件初始内容仍需进一步追踪原始驱动，
不能把 Linux 分配清零当作已验证的硬件初始化。实际 BAR 上传、上下文发布、
完成事件及复制结果都还没有在 GPU 上验证。
