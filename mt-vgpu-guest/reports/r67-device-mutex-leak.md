# r67：render-context 仅建 3 static PMR + device-mutex 泄漏事件

## CCB 证据（预期内）

rung4（render-context 创建全周期）重放：恰好 3 次 `0x6:0x9`，
annotation 为 PDS/General/USC Static Memory——kicksync CCB 与 render
context 对象都不产生 UMD 可见 PMR。r56“CCB server 侧分配”结论获独立复现。

## device-mutex 泄漏事件（本轮主要发现）

同一 run 的 UMD 内部 MapPMR 卡死在 `pvr_pmr_dma_register+0x87`
（`device_lock`，反汇编 + 重定位逐项核对），且新起的 smoke 亦挂——
`dev->mutex` 全局泄漏。只读诊断模块（零加锁、`/tmp/opencode/mtxlock/`，
不入库）从运行内核取证：

- `pci_dev+0x158 == &dev.mutex`（offset 逐项打印确认）；
- `owner=ff1fd3ea05259fc0` 非零、`wait_list` 非空；
- `for_each_process` 比对：**owner 与任何活 task_struct 无关**——
  持锁者已死，典型的 owner-death 泄漏（bA38 同类，不同 mutex）。

排查：trial_lock 正常（sysfs 秒回）；全机唯一 D 任务即受害者；
无 oops/segfault/OOM；bridge 本轮零改动；触发者无法指认——
03:12 前最后一次成功加锁是 cover-probe（02:38），其间无 bridge
ioctl 在飞；gdb kill 只落在 fabricated（无真实 ioctl）用户态。
一次诊断性 smoke 为证实全局性而新增第二个 D 任务，已停止继续试探。

## 影响与出路

- 坏：所有 MapPMR 永久挂起（不是降级，是挂起）——S4-3 DMA 路径全断，
  ladder 不可再跑；两个 D 任务清不掉，hung_task 每 2 分钟刷屏。
- 好：trial_lock、固件通道、会话（`pending=0`）丝毫无损；
  非 DMA 的 bridge 命令与 sysfs 全部正常。
- 内核 mutex 没有外部解锁 API；bridge rmmod 也解不掉它（锁属 PCI core）。
  唯一干净恢复是重启（会话重建流程已走过四次）。
- 教训：在 `timeout N` 包装下跑 bridge ioctl 本质危险——TERM 落在临界区
  内即泄漏（无论 file 锁还是 device 锁）。以后凡是 `timeout` + bridge
  ioctl 的组合，必须先论证超时后无持锁可能，或改用可中断等待。
