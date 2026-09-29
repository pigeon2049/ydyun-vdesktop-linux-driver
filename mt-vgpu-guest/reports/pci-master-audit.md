# Guest PCI 启用步骤：反编译、修正与实测

2026-09-28。本轮发现并补齐一个真实的 Windows Guest 初始化步骤，但仍未获得硬件加速。
没有操作宿主机，没有发送 BAR1+0x118 故障恢复请求，没有重置设备或追加固件命令。

## 原始指令依据

`mtkm64.sys`（SHA-256 `0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33`）
`140003af4` 将 DXGKRNL_INTERFACE 保存到 adapter+0x260，并通过 capability
ID=0xaa、tag=0xaaaa 识别 Guest，置 adapter+0x8c4=1。其结束分支：

- `140003d94`：仅在 Guest 标记非零时进入。
- `140003dc0`：`or WORD PTR [config+4], 6`，设置 MEMORY 与 MASTER，保留其他位。
- `140003dd9`：通过 adapter+0x2b8，传 DeviceHandle、DataType=0、config+4、
  Offset=4、Length=4、BytesWritten 指针，写回配置空间。
- 返回错误或 BytesWritten != 4 时再调用一次。不是持续重试循环。

根据公开 [DXGKRNL_INTERFACE 定义](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/ns-dispmprt-_dxgkrnl_interface)，
x64 下 +0x58 对应 DxgkCbWriteDeviceSpace；adapter+0x260+0x58=0x2b8。
[写配置回调定义](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_write_device_space)
也与六个实参一致。这里不是另一个私有 BAR 通知。

`scripts/verify-pci-enable.py` 在 Unicorn 内执行原始 `140003d94..140003e18`
（前序寄存器设为已知入口值，结束前截断栈恢复），覆盖 Guest/Native、四种原始 COMMAND、
成功/错误/短写三种回调结果，共 **24 案例通过**。回调仅记录，不访问设备。
见 `pci-enable-oracle.json`。这验证该片段行为，不是完整 Windows 初始化仿真。

## Linux 修正

`kernel/mt_guest_probe.c` 先前调用 `pci_enable_device_mem`，没有调用 `pci_set_master`。
现在仅 `trial_connect=1` 的干净连接路径，在确认 Guest0/FW1 后、注册共享页前调用
`pci_set_master` 并回读 MEMORY|MASTER。Linux 对这两个接口的职责说明见
[PCI 驱动文档](https://docs.kernel.org/PCI/pci.html)。

新增标志位在外层 `mt_guest_device` 末尾，`struct mt_guest` ABI 不变。
分配失败或已确认断开时清除本轮开启的 MASTER；未确认断开的 pinned 会话保留它及资源。
基础探测、离线数据准备和 `recover_channels=1` 不自动开启 MASTER。
使用 Linux 的配置字接口，不照搬 Windows 四字节写入以免一并写到 PCI STATUS。

W=1 构建通过。磁盘新主模块 SHA-256：
`c01a7bbefcfefaa3f7d01f0ad7d44f92ba7d24c2b106643dc2e1a26ddd6546c6`。
**没有替换在用的主模块**；新主模块的完整干净连接路径尚未实测。
在用模块仍是备份 SHA-256 `6f2599bec31bc199bd0dcce1bbb0caa61a022aecbe8aa92294ee3e7b87d1553b`，
其 GNU build-id 已与 `/sys/module/mt_guest_probe/notes/.note.gnu.build-id` 逐字节核对。

构建及顺序记录见 `pci-master-build.json`，旧启动顺序审计保留不覆盖。
`scripts/fresh-trial.py` 已切换到新模块散列，并明确说明正式试验会开启 BusMaster。
只读预检仅因已有绑定会话而拒绝执行，`insmod_invoked=false`，记录在
`build/recovery-channel/pci-master-fresh-preflight.json`。

## 两次有界实机试验

| 试验 | 设备写入 | 观测结果 |
| --- | --- | --- |
| 单独打开 MASTER | COMMAND.w 按位掩码 0003→0007，约 12 秒后恢复 0003 | Guest1/FW1、started=0，所有游标不变；RPC 回复 12870→12880 |
| MASTER 与已排队命令通知 | `pci_set_master`，BAR1+0x148=1，BAR0+0xb00=0；20 秒自动关闭 MASTER | 22 秒观察中仍 Guest1/FW1、started=0、DM0 5/0；RPC 回复 13040→13055 |

第二次试验针对新发现的 PCI 前置条件，不是重复执行相同条件的旧 kick。
没有重载主模块或重置其 `retained_kick_attempted` 标志。独立辅助模块
`mt_master_notify` 先只读加载，核对身份、绑定、主模块 ABI、配置、所有队列游标及
四条 connect/一条 disconnect opcode 后，才允许一次明确控制写入。
仅临时映射固件区用于读取；没有修改固件、页表、队列、Guest 状态或共享控制字段。
通知前先安排 20 秒回退工作；现场确认自动恢复 COMMAND=0003 后正常卸载辅助模块。
主模块始终保持 IRQ/RPC 服务，没有在观察期长期持有其 mutex。

试验记录：`pci-master-hardware.json`、`pci-master-notify-hardware.json`。
脚本默认只预检，已有记录后拒绝再次 `--run`。辅助模块 SHA-256：
`e2ea54466f29dc1af295f96a134ea78436c6cf499c9b2b9bebaa2c1c76d9ac0d`。
两次试验后 SDDM、spice-vdagentd、tailscaled 均 active，近期 dmesg 无新增 IRQ 故障。

## 对旧结论的修正与边界

此前仅检查主 GPU 初始化与旧 Host 配置处理，遗漏了更早的 Windows adapter 初始化。
因此“未发现 Guest 启用 BusMaster 的证据”已过时。旧 Host 2.3 对 MASTER 的处理
也不能代替当前 Guest 的明确初始化步骤。

本轮结果仅表明：**在当前跨 Guest 重启保留的失败会话中，补开 MASTER 并重发一次
既有队列通知没有让固件前进**。不能推断干净初始化不需要该步骤，也不能据此判定
是当前 Host 的哪个内部条件阻塞。未构造空 render 节点，未宣称驱动适配完成。
后续继续核查 Guest 初始化及资源生命周期，不把操作宿主或 MPC 重置作为前提。
