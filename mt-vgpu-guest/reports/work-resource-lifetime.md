# 工作任务资源持有与完成回收

本轮按“先不管恢复，继续适配”推进本机 Guest 代码，没有执行宿主操作、恢复或 GPU 工作提交。

## 实现

- `kernel/mt_work_job.h` 为完整工作包增加任务生命周期：准备、队列发布、匹配完成。
  在共同会话锁内持有页表和全部映射 BO；同一 BO 的多个地址别名只取一次 GPU 引用。
  目前按对象独占，不支持重叠资源上的并发任务。
- 准备要求页表已上传。资源获取中途遭遇 CPU 使用、GPU 使用或引用溢出，会逆序撤销
  已取得的引用，不修改输出任务。VM 的 `active_uses` 阻止绑定、解绑、重新上传和销毁。
- `mt_marker_fence.h` 新增内部 `submit_work`，统一使用每 DM 的 fence 编号覆盖调用者编号。
  先持有资源并加入 pending 队列，再写提交队列；队列拒绝（写入前错误）回滚全部持有。
- 已发布任务不能取消。关闭外部对象、丢弃 fence 或等待超时不释放 pending 资源。
  只有对应 DM 最早待完成编号的普通完成事件才能释放 GPU 引用，然后触发 dma_fence。
  错误编号、故障、抢占事件不释放。
- 任务完成不等于根页表撤回：已封存 VM 继续禁止销毁，没有增加虚构的解除封存协议。
- 主模块 `ready` 和新增 `work_ready` 均保持关闭，没有公开开关或 ioctl。
  只有独立 RAM 自测开启工作提交；实际 GPU 上的上下文关联和命令流验证仍未完成。

## 验证

`python3 scripts/verify-runtime-integration.py` 通过，包含新增 `work_job_test` 的
ASan/UBSan 资源别名、失败回滚、CPU 排斥、VM 冻结及所有者关闭测试；原有回归通过。
主模块 W=1 编译无警告，保留的 `mt_guest` ABI 对照通过，未加载主模块。

`python3 scripts/verify-gem-kernel.py --kind fence --run` 通过 **204 项**真实内核检查。
使用真实 dma_fence 与普通内核 RAM 队列、虚构 GPU 地址和模拟完成事件：
包括完整工作包、队列满回滚、映射别名、引用溢出、外部引用关闭、超时、错误编号、
故障事件和正确完成。三个 BO 分配/释放平衡。临时测试模块已卸载，无内核告警。
测试前后 PCI 均未绑定主实验驱动，主模块不存在，DRM 仍仅有 QXL card0。

- 主模块 SHA256：`a0b347b38f5d44e3e635dec0a6fb087856697ae6e590de0ad28f733def9f8852`
- fence 自测 SHA256：`6e089508e3144f5295ac276ec2fbcfde1f5bf78dac2b2d1a8394f0d8b36de47b`
- 机器结果：[构建与离线测试](runtime-integration-build.json)、[真实内核 RAM 测试](fence-kernel-validation.json)

## 仍需适配

下一步继续追踪任务上下文创建、根页表关联与固件上下文对象结构；然后才能接入对应的
GEM 文件接口、任务调度和真实命令流。当前结果只证明软件资源管理路径，**不证明固件
接受了任务，也未实现摩尔线程硬件加速**。
