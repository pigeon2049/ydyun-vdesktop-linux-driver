# r25：Guest 重启后的固件保留现场

2026-09-30，北京时间。boot ID `cfb0c74d-c9e3-4804-bc5e-dbf1e7f07c0b`，
内核 `6.12.107+deb13-amd64`，设备 `0000:00:0e.0`。

## 本轮结论

用户已经重启 Guest。本次启动后观测为 `Guest=0 / FW=0`，固件尚未 READY。
读取的完整 8 MiB 固件区与 r23 失败提交之后的快照逐字节相同：

`defd19804f27295aa47fbc6d373dc4c00b36c0953b950cf2729c79bd0b56958d`

`started=1`；DM1 命令 head/tail=2/1，事件 head/tail=2/1，
未消费事件仍为 `[0,257,2,0,0,0]`。此次重启没有清空该显存故障现场。
这不证明 Host 内部所有状态均未变化，也不证明重启后的旧 Guest GPA 仍然有效。

r24 页表权限候选未加载、未提交。当前仍无 MTT 硬件加速。
继续运行 fresh trial 的先决条件仍是 Guest OFF / FW READY；没有通过改写状态值绕过。

## 实际操作

开机自动加载的是已安装的旧 `mtgpu`，仍因 `PhysHeapsInit [644]` 失败，
没有建立 MTT DRM 节点。其引用数为零，已正常卸载。卸载时出现原模块
`pvr_sync_deinit` 的 system-wide workqueue flush 警告；不是新增快照 helper 的 Oops。
未改开机加载配置。因此当前无法区分旧自动加载驱动是否影响了首次观测的 FW 状态。

`mt_offline_info` 完成同步 V2 信息页查询，得到 OSID 6、flags `0x3d1`、原六段布局。
查询前后均 Guest0/FW0。随后增加可选 `capture_retained=1`：

- 拒绝绑定中的设备，检查精确 PCI 身份、BAR0/1 大小、Memory 开启且 BusMaster 关闭。
- 验证 fresh V2 信息页的 OSID、flags、段表、BAR2 实际长度和 2 MiB 前缀。
- 校验只读发布值：BAR2 GPA `0x800000000`、local MMU GPA 0、
  FW GPU PA `0x769fef000`、长度 `0x800000`。
- 由段表推导 BAR2 offset `0x3f000000`，临时保留 BAR2，读取 8 MiB，随即释放映射和区域。
- 仅信息查询向 BAR1+0xc8 写入新分配页的 GPA；不写 BAR2、状态、队列、固件发布或 doorbell，
  不引用旧 Guest RAM，不触发 reset。

W=1 构建无警告；helper 实际加载、导出快照、正常卸载成功。最终 PCI 无驱动绑定，
`mtgpu`、`mt_guest_probe`、`mt_live_tqx`、`mt_offline_info` 均未加载。

## 0x101 原始处理分支

新增 `scripts/verify-r25-fault-event.py`，在 Unicorn 普通 RAM 内执行
Windows `14000e5c8`，只拦截清零和 OS 通知回调。121 个案例通过：
节点模式 0/2/6/7/8、三个节点索引、软件环索引 0/63、空队列、编号不匹配及 NULL context。

只有模式 7、存在待处理项且 fence 匹配时，该分支才更新 context 完成编号、
置 context+0x38、推进软件索引并通知 OS。实际事件 fence=2 产生的通知中：
type=9、fence=2、flags=9、fault VA=0、page-table-level=1、error-code=0。
测试中的 node=9 / engine=3 来自人工构造上下文，不是本机拓扑测量。

微软定义 type 9 为 `DXGK_INTERRUPT_DMA_PAGE_FAULTED`，用于通知需要 OS 恢复的 GPU 错误。
但参考处理器把 flags、level、VA 等写成常量，不能据此认定真实故障 VA、页表级别或唯一原因。
因此 r24 的只读 PTE 错误仍是已证实缺陷，而“修正后一定能复制”仍待硬件验证。
依据：[通知类型](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_interrupt_type)、
[通知结构](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkargcb_notify_interrupt_data)。

继续检查的 `1411c9f58 → 1411c9c54` ResetEngine 路径不是一个可以直接照写的 MMIO reset：
它依赖 Windows 现存软件上下文；若仍有 pending，进入 `140017110(...,0)` 的固件卸载/加载流程。
重启后本 Linux Guest 已失去旧软件上下文和 RAM 所有权，不能直接照搬这一处理并释放旧资源。
后续 r26 核查又明确：在适用平台回调启用时，`140017110` 会先进入已有 HWR 请求链，
不能将此 ResetEngine 名称理解为独立 Guest 恢复。见 [完整边界核查](r26-mapping-and-recovery-boundary.md)。

上线通知 `14002249c` 只有 BAR1+0x148=1。旧 Host 2.3 的 BAR1 handler 先调用扩展回调，
默认分支没有该 offset 的直接处理；不能把旧 Host 代码视为当前 Host 对上线通知的完整证明。
没有在当前离线故障会话上发通知、提交 DISCONNECT、消费旧事件或上传新固件。
没有执行已有的 MPC 范围 HWR 入口，全部操作留在 Guest。

## 产物和复查

- `reports/r25-boot-dmesg.log`、`r25-after-snapshot-dmesg.log`：启动、卸载和采集日志。
- `reports/r25-offline-info.bin`、`r25-snapshot-info.bin`：两次查询的原始 4096 字节。
- `reports/r25-retained-firmware.bin`：本次重启后读出的 8 MiB 原始固件。
- `reports/r25-retained-comparison.json`：含本次 boot ID 的比较结果。
- `reports/r25-retained-decoded.json`：完整队列和待处理事件解析。
- `reports/r25-fault-event.json`：原始故障处理指令的受限执行结果。
- `reports/r25-final-preflight.json`：FW0 拒绝试验，`insmod_invoked=false`。

```sh
python3 scripts/compare-retained-firmware.py reports/r25-retained-firmware.bin \
  --compare build/r23-live/tqx-after-trial_firmware
python3 scripts/verify-r25-fault-event.py
```

后续工作仍为 Guest-only 适配。恢复研究需先证明 VM 范围内的完整协议及旧地址撤销，
不能仅凭普通 Guest reboot、寄存器写回或事件确认认定资源可安全复用。
