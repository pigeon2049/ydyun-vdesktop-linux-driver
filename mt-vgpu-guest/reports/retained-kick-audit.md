# 恢复通信后的固件通知复查

2026-09-28，本轮没有操作宿主、重启 Guest、上传固件或增加命令。
目标仍是使本机 vGPU 可用；当前没有硬件加速。

## 新的实机证据

新增 `publication` 只读属性，重启后回读仍为 BAR2 GPA `0x800000000`、BAR4 GPA 0、
固件 GPU PA `0x771fef000`、固件长度 `0x800000`。这些字段没有因 Guest 重启丢失，
但不能证明 Host 内部映射仍有效。

新增 `retained_status` 只读属性，每次临时保留 BAR2 并读取已验证的固件范围，
读取后撤销 CPU 映射/释放临时 BAR reservation。它不修改任何固件数据或队列，
也不消费事件。不同于 `memory_raw`，这是实时观察，不是绑定时快照。

增加严格限定的 `retained_control`：只接受 `kick`，只允许恢复模式、通信就绪、
Guest1/FW1、started=0、已知发布地址、DM0 队列 5/0、其余队列全空，以及前五条
opcode 恰为 46/46/46/46/47。每次模块加载最多执行一次。
操作只有 BAR1+0x148 写 1 和 BAR0+0xb00 写 0，保留原五条请求，不写固件、
队列头尾或 Guest 状态；随后 400 次、每次请求睡眠 25 ms，期间继续处理 RPC。

本机一次测试完成，实际耗时约 13.2 秒（调度与执行开销使其超过名义 10 秒）。
最终仍为 Guest1/FW1、started=0、head=5/tail=0。期间完成 10 次中断确认和查询回复，
rejected=0；第二次写被 EALREADY 拒绝。随后 25 秒实时采样没有队列变化。

这排除了“通信已恢复，只需再补一次通知即可推进”的本次假设，但不能在 Guest
观察范围内区分 Host 映射失效、固件兼容性或其他内部启动条件。下一步应围绕这些
差异寻找证据，不能通过反复通知或人为改状态值来宣称启动成功。

## 参考代码核查

- Windows `140016218` 的首次加载路径发布 GPU PA/长度，然后调用 `1400168b4`。
  `1400174fc` 仅在已有固件上下文时选 Hard Reset；不能把其跳过连接的返回值当成启动成功。
- `14002249c` 是 BAR1+0x148 写 1；命令通知使用 DM 索引写 BAR0+0xb00。
- 旧 Host 2.3 `vgpu_access_pci_bar0_region` 的 `4d3ec` 分支转入
  `mtgpu_mdev_vgpu_state_kick`。后者对 DM0 在通知环加入 `osid << 9 | value`，
  再写硬件 doorbell `value | 0x104`。这一步在 Host 内完成，Guest 不应自行
  将原来的 0 改成 0x104。旧 Host 与当前 Host 不同，不能照抄其内部结构偏移。

## 可复查产物

- `build/recovery-channel/live-readback-validation.json`：恢复模式正常卸载/重绑后的首次实时回读。
- `build/recovery-channel/retained-kick-preflight.json`：通知前状态及新模块散列。
- `build/recovery-channel/retained-kick-validation.json`：唯一一次通知、重复保护、前后状态。
- `build/recovery-channel/retained-kick-kernel.log`：本轮完整内核日志。
- `reports/retained-kick-audit.json`：最后实时采样、服务/IRQ 状态及源代码散列。

模块 W=1 编译成功，无编译警告。`scripts/capture-recovery.py` 已收集新增只读属性。
当前保持通信服务运行，无本轮新增固件资源，不安装开机配置；任务尚未完成。
