# VM 销毁的引用预检与回归

本轮在 VM/BO 生命周期适配中补齐了失败原子性。`mt_gpu_vm_fini` 先确认 VM 未封存、无活跃使用/进程所有者，
然后按 BO 聚合 VM 持有的映射引用数量，并核对当前引用至少覆盖这些映射和已有 CPU/GPU pin。页表 BO 也先行核验。
任何 BO 记录为空、与页表 BO 别名或引用账目不足时，在归还引用之前返回 `-EUCLEAN`；只有全组通过后才释放 VM 引用并清空 VM。

这保护的是内部生命周期不变量，正常 BO 操作应始终维持上述引用关系。它不提供页表撤回、TLB 失效或封存 VM 销毁；
封存根和活跃使用仍返回 `-EBUSY`。工作负载准入仍关闭。

内核 fence 自测新增一项故意破坏后序 BO 引用数的 RAM 案例，确认页表及先前映射引用未被部分释放；本次共 **322 项检查通过**。
GEM 自测 **280 项检查通过**，并将重复绑定完整启动映射的旧 `-EEXIST` 断言改为幂等成功，以匹配池缓存路径。
两个测试模块均已卸载，临时 sysfs 设备已移除，测试前后的 PCI 绑定和 `/dev/dri` 节点列表一致。
只使用普通系统 RAM 队列/后备，没有映射 MTT BAR、访问 GPU 或执行设备工作。

- 主模块 `W=1` 构建通过，SHA-256：`9e8eb7ed684900b379d0a3d7d4c9986d3000ea5decc1fbeeab1815d85af864c1`
- fence RAM 自测 SHA-256：`cdebd8e90df9b57229d1212f3c50d4f8bf8c073bc070442cf47a93f35fa80d5e`
- GEM RAM 自测 SHA-256：`3a7d47c695ab714313391c884421005c98824c67d243c2fd3084aad887d6467e`

机器可读结果见 `fence-kernel-validation.json` 与 `gem-kernel-validation.json`。
