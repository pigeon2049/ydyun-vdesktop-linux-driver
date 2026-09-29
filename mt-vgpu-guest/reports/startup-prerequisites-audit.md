# 固件加载前置条件核查（2026-09-28）

**后续修正**：较早的 adapter 初始化 `140003af4` 确实包含 Guest COMMAND |= 6。
已完成原始指令验证、Linux 修正以及仅限本 VM 的两次有界试验，仍未推动保留固件会话。
详见 [最新 PCI 启用核查](pci-master-audit.md)。下文为此前取证，COMMAND=0403、
自持引用以及需要宿主重建等描述均为历史记录，不能作为当前操作指引；用户已明确
宿主无法操作，后续工作仅在本机进行。

当前仍 Guest=1 / FW=1 / started=0，未验证硬件加速。本轮没有写设备。

## Windows Guest 主路径

从已保存的 mtkm64.sys 反编译库追踪 `14000de94`：

| 入口 | 当前已核实的作用 | 对本机下一步的影响 |
| --- | --- | --- |
| `1400230e0 -> 140026064` | 平台映射、共享通道协商、设备信息、私有池、WDDM 分段 | 已补齐的通知与协商顺序仍需干净启动实测 |
| `14002d950` | S3000 平台回调表；Guest 不进入 Native 的寄存器/PCIe DMA 回调配置分支 | 不能把 Native 启动寄存器移植到 Guest 来猜写 |
| `140025338` | Guest 直接取设备信息 `+c9c`；Native 才读特征寄存器 | 未发现这里存在遗漏的 Guest 启动写入 |
| `140026d20` | Windows HardwareInformation 注册表信息更新 | 不是固件上线命令 |
| `1411ce6b8 -> 14002c944` | 构造 WDDM 显存段描述并转换分段地址 | 输出为 Windows 段描述，不能当作新 MMIO 命令 |
| `140021b04` | 调用私有内存分配器构造函数 | 固件分配地址已由完整加载入口对照覆盖；不等于全部资源生命周期已实现 |
| `14001ca58 -> 140001d10` | 后者直接返回 0，随后初始化 Windows 内存管理描述 | 没有在此发现漏掉的 PCI 启用动作 |
| `14000b790 -> 140012a04 / 14000b4d8` | 创建工作线程、事件、上下文缓存和锁 | 持续 IRQ/RPC 已补入 Linux，但新主模块首次启动尚未验证 |
| `14000ee38` | 分配并清零 0x18 字节 Guest 统计结构 | 不是固件连接所需的 Host 通知 |
| `1400174fc -> 140016dcc -> 140016218` | 加载固件并连接，成功后才发布运行上下文 | 连接后 BAR1+f0/+70 不能提前猜写 |

这是一组调用路径的静态核查，不是整个 Windows 启动环境的完整仿真。

## PCI COMMAND / BusMaster 假设

本机只读 PCI COMMAND=0x0403，BusMaster 关闭。官方旧 Host 2.3.0 对象中：

- `vgpu_access_pci_config_region`（ELF .text `0x4add0..0x4afd5`）的特殊写分支
  仅针对 BAR0..5 与 ROM；其他字段按写掩码更新虚拟配置字节。
- 对照 `inc/common/os-interface.h` 的 `DECLEAR_OS_VALUE`，PC32 relocation 的
  `os_value+0x1bc` 等值需要加 4 再除以 8，分别对应 BAR0..5；不是 PCI_COMMAND。
- `vgpu_configure_space_init` 的 `0x4cdca..0x4cde2` 初始化 COMMAND 为 IO|MEMORY，
  不包含 MASTER。此代码不支持“必须先开 BusMaster 才能接收 Guest 启动通知”的假设。

旧 Host 不是当前 Host 的二进制，因此不能据此排除当前实现有其他门槛；
本轮未修改 COMMAND，没有把缺少证据当成硬件兼容性结论。

## 下一次实机试验

当前旧模块断开未确认，主模块自持引用且辅助模块仍服务消息，不能原地替换。
本机未提供 PCI reset/reset_method，尚无 Guest 内可确认完成的设备重置路径。
需要宿主确认释放旧 vGPU 会话并重新创建；Guest 内的普通 reboot 不保证这一点。

新版已编译模块：`kernel/mt_guest_probe.ko`，SHA-256
`aa4644a8bfb3173a25e47dbb0873d16d01ad5964eb70eba7f84664c49d818bac`。
`scripts/fresh-trial.py` 默认只检查；模块已绑定时不读 BAR，不替换模块。
只有显式 `--run` 且身份、固件、内核/模块散列、未绑定和 Guest0/FW1 检查通过时，
才运行一次完整连接/断开试验，保存固件、上传前备份、共享页、状态和内核日志。
脚本不执行重启、卸载、解绑、PCI 配置修改或启动配置安装。
模块加载成功与固件连接成功分别记录；未连接时返回非零，保留资源供诊断。

本机预检见 `fresh-trial-preflight.json`：身份/模块/固件符合，旧会话仍存在，
`eligible_for_trial=false`、`insmod_invoked=false`。干净会话的实际执行尚未验证。
