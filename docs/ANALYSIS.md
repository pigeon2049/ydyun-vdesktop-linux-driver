# YDYUN Windows 驱动静态分析记录

更新时间：2026-09-22（Asia/Shanghai）

## Step 0：范围和方法

分析对象：`/opt/code/ydyun/driver/`

方法：文件清单、`file`、`7z l/x`、PE/驱动 ASCII/UTF-16 字符串、INF 配置、SHA-256。原始 PE/驱动没有被执行；嵌套安装包只做了解压。

解压结果：`doc/extracted/`

## Step 1：原始包形态

根目录有 61 个 `.exe`、67 个 `.dll`、4 个 `.pdb`，未发现源码工程文件。核心安装包包括：

| 包 | 推断用途 |
|---|---|
| `vdesktop/VDesktop-setup-usbV7.25.43SP1.exe` | USB 转发安装包，内含 `guestos-usb.7z` |
| `vdesktop/VDesktop-setup-commV7.25.43SP1.exe` | 通信组件，内含 `ZProcessMonitor` 等 |
| `vdesktop/VDesktop-setup-sysguardV7.25.43SP1.exe` | 系统安全/监控组件 |
| `vdesktop/VDesktop-setup-mediaV7.25.43SP1.exe` | 摄像头/媒体重定向 |
| `pv/zte-guesttools-setup-V3.18.56.215a7089-Release.exe` | VirtIO/QEMU guest tools |

版本线索：`packageversion.ini` 中 `vmbooster_version=V7.25.43SP1-73`，`pvdriver_version=215a7089`。

## Step 2：USB 核心包

USB 包的核心文件位于 `doc/extracted/usb/nested/`：

| 文件 | 证据/作用 |
|---|---|
| `usb/amd64/UsbIpc.exe` | 主 USB/IP 用户态服务；处理设备导入、USB/IP 数据、压缩、端口及外围设备扩展 |
| `usb/x86/UsbIpc.exe` | 32 位同类服务 |
| `usb/x86/UsbIpcGuard.exe` | USB 服务守护、服务修复、设备状态管理和打印机枚举 |
| `usbipenum/USBIPEnum_x64.sys` | 旧 USB/IP 虚拟总线枚举驱动 |
| `ude/usb_vhci_client.sys` | 新 UDE/KMDF 虚拟 USB 主控制器客户端 |
| `ude/usb_vhci_filter.sys` | USB 上层过滤驱动 |
| `vhci/zteusbb_x64.sys` | ZTE 旧虚拟 USB 总线 |
| `vdiskmrx/vdiskmrx_x64.sys` | Windows RDBSS 网络重定向文件系统，非 USB Mass Storage 核心 |
| `usb/amd64/WinDivert64.sys` | Windows 网络包拦截，主要被打印机/扫描仪路径使用 |

USB 相关 `.sys/.inf/.cat` 已全部保存在 `doc/extracted/usb/nested/`，具体清单可用：

```sh
find doc/extracted/usb/nested -type f \( -iname '*.sys' -o -iname '*.inf' -o -iname '*.cat' \)
```

## Step 3：INF 证据

### USBIPEnum

`usbipenum/USBIPEnum.inf` 的注释直接写明：`INF file for installing USB/IP bus enumerator driver`。硬件 ID 是 `root\\USBIPEnum`，服务名是 `USBIPEnum`，驱动文件被重命名为 `USBIPEnum.sys`。

### ZTE VHCI

`vhci/zteusbb.inf` 使用 `root\\zteusbb`，服务名 `zteusbb`，设备描述为 `ZTE USB host controller`。

### UDE VHCI

`ude/usb_vhci_client.inf` 使用 PCI ID `PCI\\VEN_19D2&DEV_A36D&SUBSYS_00000001&REV_10`，服务名 `usb_vhci_client`，要求 KMDF 1.15。

### USB filter

`ude/usb_vhci_filter.inf` 会向 USB 类注册 `UpperFilters=usb_vhci_filter`。它是 Windows 栈集成辅助件，Linux 不应按原样移植；Linux 侧由 `vhci-hcd` 和 usbip 内核接口承担同类功能。

### 其他总线

包内还包括 `vserial_bus`、`win_vhci/cam_bus`、VirtualScanner WIA 驱动。它们分别属于虚拟串口、虚拟摄像头和虚拟扫描仪，不是本阶段的 USB 存储/HID 必需件。

## Step 4：协议证据

`UsbIpc.exe` 的字符串包括：

- `usbip_header`、`usbip_header_correct_endian`
- `usbip_send_op_common`、`usbip_recv_op_common`
- `usbip_xmit`、`usbip_recv`
- `URB_FUNCTION_CONTROL_TRANSFER`
- `URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER`
- `URB_FUNCTION_ISOCH_TRANSFER`
- `seqnum`、`devid`、`ep`、`attach_device_as_server`
- `usbip_vbus_attach_device`、`usbip_vbus_detach_device`
- `usbip_win_vhci_attach_device`、`usbip_win_vhci_detach_device`
- `usbip_vbus_forward`
- `compressData`、`deCompressData`、`lzma compress`、`miniLzo`

同时存在厂商扩展线索：

- `USBIP_REDIRECT_UNIFIED`
- `success send usbipc redirect capability`
- `Nat_Port_In_3246`、`Nat_Port_In_5100`、`Nat_Port_In_19000`
- `DISCOV_*`、`NP_DISCOV_*`
- `linux attach device`、`linux dattach device`、`linux dattach all devices`

结论：数据面高度兼容 USB/IP 概念，控制面包含 ZTE 云桌面扩展；标准 Linux `usbip` 可作为第一版底座，但真实互通仍需要端到端会话验证。

### 与公开 USB/IP 实现的比对

为避免仅凭字符串下结论，已将公开的 `cezanne/usbip-win` 源码以只读参考方式保存到 `doc/reference/usbip-win/`，版本：`68001ff23bf53fbbaa81db54d35d1a29a662e183`，许可证为 GPL-3.0。没有把该仓库源码复制进 Linux 产物。

公开实现定义的标准控制面为：

- 8 字节 `op_common`：version、operation code、status；
- `OP_REQ_DEVLIST`：请求远端设备列表；
- `OP_REQ_IMPORT`：按 bus ID 导入设备；
- 默认 TCP 端口 `3240`；
- 后续 USB 传输使用 `CMD_SUBMIT/RET_SUBMIT/CMD_UNLINK/RET_UNLINK` 和 URB 头。

原始 `UsbIpc.exe` 同样出现 `sending op_common`、`recv op_devlist`、`query_import_device`、`send OP_REQ_IMPORT`、`recv op_import_reply`、`version mismatch` 等字符串，因此标准 USB/IP attach 路径不是推测，而是有直接二进制证据支持的。

### ZTE 扩展和压缩

原始 `UsbIpc.exe` 还出现 `USBIP_REDIRECT_UNIFIED`、`handle_usb_redirect_capability`、`compress_flag`、`check_compress_header`、`deal_compress_submit`。这说明 ZTE 可能在标准 USB/IP 控制面外协商“统一重定向”能力，并对部分 `CMD_SUBMIT/RET_SUBMIT` 做可选压缩。

因此当前 Linux 控制器采取以下策略：

1. 先使用发行版 `usbip` 的标准 `DEVLIST/IMPORT` 和非压缩 USB/IP 数据面。
2. 端口可配置为标准 `3240` 或 ZTE 包明确放行的候选端口 `3246`。
3. 不伪造未知的 `USBIP_REDIRECT_UNIFIED` capability，也不默认启用压缩；否则可能把网络打印机、扫描仪、虚拟摄像头扩展误接入 USB 数据路径。
4. 只有真实会话证明服务要求压缩时，才实现严格长度上限、序号匹配和压缩算法白名单的兼容层。

## Step 5：安全/监控/遥测识别

### 明确属于安全/监控/遥测的部分

- `vmsecurity/asiainfo/forcloud_agentless_sensor-20.0.2.624.exe`
- `vmsecurity/qianxin/VMSecNotifier.exe`
- `vdesktop/VDesktop-setup-sysguardV7.25.43SP1.exe` 及 `SysGuard.exe`、`GuardAgent.exe`、`SysMonitorBall.exe`
- `comm/nested/ZProcessMonitor/ZProcessMonitor.sys`
- `comm/nested/ZProcessMonitor/ServiceChecker.exe`
- `usb/nested/usb/x86/UsbIpcGuard.exe`
- `usb/nested/usb/amd64/vmusbtrace_x64.dll`
- `qoelog.dll`、`qoelog_x64.dll`

`UsbIpcGuard.exe` 的字符串表明它会检查并修复 USBIP Client 服务、启动/终止 `UsbIpc.exe`、枚举打印机并记录 dump；它不是 USB 数据面本身，首版 Linux 适配排除。

### 需要保留的基础安全边界

不移植上述监控模块，不代表删除所有安全措施。Linux 实现至少保留：

- 仅连接配置的云端地址和端口；
- 控制消息长度、设备号、序号、端点和传输方向校验；
- 传输超时、取消和断线清理；
- systemd 限权和设备访问规则；
- 默认不开放调试 dump、遥测和任意设备自动导出。

## Step 6：Linux 适配策略

Linux 不需要移植这些 Windows `.sys`：

| Windows 能力 | Debian 侧实现 |
|---|---|
| USB/IP 虚拟总线 | 内核 `usbip-core` + `vhci-hcd` |
| USB 存储 | 内核 `usb-storage` / `uas`，由远端 USB 描述符正常枚举 |
| 鼠标/键盘 | 内核 `usbhid`、`hid-generic`，无需额外 HID 驱动 |
| USB/IP 控制器 | 新增小型用户态 `ydyun-usbctl` |
| Windows 服务守护 | systemd service + 失败重连，不引入进程监控 |
| Windows INF/CAT/签名 | Debian packaging、模块由发行版内核提供 |

首版不包含：USB filter、WinDivert、vdiskmrx、虚拟串口、摄像头、扫描仪、打印机、QoE/trace、安全监控。

## Step 7：完整性记录

关键文件 SHA-256：

```text
UsbIpc.exe              2bc50e4f5a7eff69865420833da0c494f4719e77354299323147ba7f31748d44
USBIPEnum_x64.sys       82c85af6a4fa40af4cbf2fa7873560677f21bd3a72caf4477ffee1cd8faa15f2
usb_vhci_client.sys     61ad0380abf954474dc5fc56e36a24150d475d6e0bfb880c3121c5b5aa9fc920
usb_vhci_filter.sys     fc090da68560cd632ffc2b0b5d04b0e5217fbb908d27083bd0a6c209f149a0e2
UsbIpcGuard.exe         df7f858f5b108dc00130501571eadaa908edca455849894eaff4cf401cb54266
WinDivert64.sys         8da085332782708d8767bcace5327a6ec7283c17cfb85e40b03cd2323a90ddc2
vmusbtrace_x64.dll      dcb9e5323c68a35d336f93dc816dae18274c98f7a26d1211d0bff16b5700b3bc
```

## 当前可下结论

1. 这是 ZTE ZXCLOUD iRAI 云桌面 guest 侧组件，版本线索为 `V7.25.43SP1`。
2. USB 存储和 HID 转发的技术核心是 USB/IP + 虚拟 HCI，而非厂商专有的块设备或输入驱动。
3. Windows 驱动二进制没有源码，无法直接为 Linux 编译；直接加载 `.sys` 在 Linux 上不可行。
4. 最小可行 Linux 方案是基于 Debian 内核 usbip/vhci-hcd 的用户态控制器；只有当标准 USB/IP 与云端控制面不互通时，才需要继续逆向 ZTE 扩展消息。
5. 用户要求移除的安全和监控部分已从适配范围排除，但实现仍会保留设备和网络边界校验。

## Step 10：私有控制面进一步静态还原

对 `usb/amd64/UsbIpc.exe` 的 `.text` 做了只读反汇编，确认 `USBIP_REDIRECT_UNIFIED`
并不是标准 USB/IP `op_common` 的简单别名：

- 一个私有接收循环按固定上限接收 `0x210` 字节消息，然后进入统一命令分发器；
- 分发器从消息结构偏移 `+8` 读取 `op_cmd`，另一个字段用于日志中的 `extra`；
- 已观察到命令值 `1`、`2`、`3`、`4`、`5`、`7`、`8`、`9`、`10`、`11`、`12`、`13`、`14`、`15`、`16`、`17`、`18`、`19`、`20`、`21` 的不同处理分支；
- 分支包含 `linux attach device`、`linux dattach device`、`dettach all devices`、能力回复和统一结果回复等控制行为；
- 私有控制 socket 的建立、初始握手和后续消息收发与标准 USB/IP 的 `DEVLIST/IMPORT` 数据 socket 分开。

因此目前不把这些未知报文写入 Linux 客户端：它们可能负责会话授权、设备映射或厂商外围设备扩展，不能只凭字符串和局部反汇编安全复刻。Linux 第一版只接入发行版 `usbip` 的标准路径；待有真实云会话时，再用固定端口、抓包和长度/状态校验逐个增加兼容。这个结论也支持首版排除安全/监控/打印机/扫描仪/摄像头相关通道。

## Step 11：本机 Debian 验证

在当前 Debian trixie x86_64 主机上完成了以下验证：

- 通过发行版仓库安装 `usbip`，实际可执行文件为 `/usr/sbin/usbip`；
- `modprobe usbip-core` 和 `modprobe vhci-hcd` 成功，内核创建了 `vhci_hcd.0` 到 `vhci_hcd.7`；
- 安装 `build/ydyun-usbctl_0.1.0-1_amd64.deb` 成功；
- `systemd-analyze verify ydyun-usbctl.service` 无报错；
- 空的示例配置对 `127.0.0.1:3240` 执行 `check` 时按预期返回连接拒绝，未自动连接或导入未知设备；
- `usbip port` 显示没有已导入设备，说明验证过程没有改变 USB 设备状态。

这证明 Debian 本地安装链路和 VHCI 基础设施可用，但不等价于云端互通；最终存储/HID 测试仍必须在有实际云会话和导出 bus ID 的环境中进行。

## Step 12：最终产物审计

最终 `build/ydyun-usbctl_0.1.0-1_amd64.deb` 的依赖只有 `python3` 和 Debian `usbip`，包内没有 `.sys`、安全代理、进程监控、QoE/trace 或 WinDivert 文件；配置、systemd 单元、控制器和分析文档均已打包。当前本机安装状态为 `usbip 2.0+6.12.107-1`、`ydyun-usbctl 0.1.0-1`，`usbip_core` 与 `vhci_hcd` 已加载，VHCI 端口为空。

最终包 SHA-256 在每次构建后写入 `doc/PROGRESS.md`。这里不重复嵌入包自身的
哈希，因为 `ANALYSIS.md` 同时作为包内文档；否则会形成自引用哈希。

## Step 13：3246 反向链路与标准 USB/IP 的关系

本轮对 `UsbIpc.exe` 的监听线程和统一控制分发器做了更细的只读反汇编，得到以下比字符串搜索更可靠的结论：

- 程序创建 `AF_INET/SOCK_STREAM` 监听 socket，并把端口常量 `0xcae`（十进制 3246）写入 `sockaddr` 后执行 `bind/listen/accept`；相关日志字符串明确称该线程为 `usbipc_backward_link_server`。
- 新连接首先接收固定的 `0x210` 字节初始消息，并按消息头的 `op_cmd` 分发。消息头至少包含版本、长度、命令和扩展字段；控制分支还会检查对端地址的网络字节序端口/IP。
- `op_cmd` 为 17/18 的分支是 USB 重定向能力探测/能力回复，能力字段包含摄像头、串口、网络打印机和扫描仪等外围通道；这些不是首版存储/HID 适配范围。
- USB 相关的若干控制分支在同一连接上继续调用标准 USB/IP 的 `OP_REQ_DEVLIST (0x8005)` 和 `OP_REQ_IMPORT (0x8003)`。这说明 3246 不是“普通标准 USB/IP 3240 端口”的简单替换，而是“私有控制前置 + 标准 USB/IP 数据阶段”的复合链路。
- 设备映射处理函数会访问初始控制对象之外的扩展区域（例如约 `+0x190`、`+0x290` 的字符串/设备字段），而监听线程的初始读取只覆盖 `0x210` 字节。扩展数据的继续接收、字段语义、导入/取消操作和压缩标志的完整布局，当前没有真实会话字节流可以交叉验证。
- `usbipc.ini` 中 `[LISTEN_5100] LISTEN_OPEN=0`，因此 5100 是可选且默认关闭的监听项；原包中的 3246/5100/19000 防火墙/NAT 字符串不能证明这些端口都是标准 USB/IP 数据端口。

因此 Linux 适配的实现决策是：

1. 保留已验证的标准路径：Debian `usbip` + `usbip-core` + `vhci-hcd`，由 `usb-storage/UAS/usbhid` 接管存储、鼠标和键盘。
2. 不把默认端口擅自改成 3246，也不把未知扩展对象按猜测偏移转换成 `usbip attach` 参数。
3. 3246 反向链路桥已经实现为默认关闭的可选路径：验证控制头和长度、拒绝能力/打印机/扫描仪/安全类命令、复用同一 TCP 连接进入标准 USB/IP；剩余工作是用真实会话确认设备 ID、导入/取消时序和断线行为。
4. 在获得真实会话或厂商协议样本前，不实现猜测性的私有控制报文，也不将安全、监控、QoE、网络拦截和外围设备模块带入 Debian 包。

## Step 14：Linux 反向链路桥实现审计

已将上述边界落实为 `ydyun-usbctl backward` 和独立的
`ydyun-usbctl-backward.service`，但默认关闭：

- 监听端口和本机代理端口均由配置显式指定，默认分别为 `0.0.0.0:3246` 和
  `127.0.0.1:3247`；桥不会自行开启，也不会替代标准 `watch` 服务。
- 远端连接必须先完整收到 0x210 字节；桥只验证静态反汇编已确认的 `0x0202`
  两字节版本标记和小端 `op_cmd` 1/2。长度不足、版本错误、能力/外围命令和
  未知命令都会关闭连接。
- 通过校验后，桥默认不修改后续 USB/IP 字节，只在远端连接和本机回环连接之间转发；
  本机 `usbip attach/watch` 仍负责调用 `vhci-hcd`。这样 Linux 的 USB 存储、UAS、
  鼠标和键盘仍由内核原生类驱动接管。
- op 2 在自动模式关闭时也不会被错误地当作导入请求转发，而是记录后关闭；只有
  显式开启 `auto_attach` 才执行对应 bus ID 的本地 detach。
- 当显式设置 `[backward] auto_attach=yes` 时，op 1 会使用已校验的 `+0x190` bus ID
  调用本机 `usbip attach`，op 2 会按相同 bus ID 查询并 detach VHCI 端口。自动模式
  会读取本地标准 `OP_REQ_IMPORT` 的 32 字节 bus ID 并按 ID 配对；非 IMPORT 请求才
  回退到 FIFO。控制帧接收和设备导入分别使用 `frame_timeout` 与 `attach_timeout`；
  真实多设备会话仍需验收；默认自动值是 `no`。
- 单元测试覆盖版本/命令拒绝、配置显式开启和双向字节转发；没有把这些测试冒充
  为云端设备导入测试。

## Step 15：3246 bus ID 字段还原

继续对 op 1/2 处理函数做静态反汇编后，已经可以安全提取一个设备标识字段，但仍
不能据此宣称完成完整云会话兼容：

- Windows 在收到 0x210 字节后，把最后一个字节 `+0x20f` 强制写成 NUL，然后将
  `+0x190` 开始的字符串传给日志格式 `busid [%s]`；因此该字段在当前帧内最大可
  读长度为 `0x7f` 字节。
- 分发表中 op 1 跳到创建/导入设备状态的处理路径，op 2 跳到按 bus ID 清理设备
  状态并关闭连接的路径；这为 Linux 端的 attach/detach 分流提供了比日志名称更强
  的静态证据。
- 同一函数还把帧首 DWORD 与 `+0x04` 的最多 4 字节字符串按 `%u-%s` 构造一个
  Linux 风格候选 bus ID。这个候选值的具体用途是内部设备状态查找，不能替代
  `+0x190` 字段，也不能在未知值上猜测设备。
- 标准 USB/IP 的 `OP_REQ_IMPORT (0x8003)` 请求体在只读参考实现中定义为
  `char busid[32]`；桥会读取这 32 字节后按 bus ID 选择待配对的 3246 连接，避免
  多设备同时 attach 时仅按到达顺序错配。
- Linux 桥现在对 `+0x190` 字段做 ASCII、长度和 bus ID 字符校验并写入日志；只有
  显式启用 `auto_attach` 时才把它传给参数数组形式的 `usbip attach`，不会经过 shell。
  默认关闭自动导入，避免在没有真实会话确认时把错误设备导入。

这一步把“帧中存在 bus ID”从字符串推测提升为字段级静态证据，并完成了可选的按
bus ID 自动 attach/detach；本地 IMPORT 请求的 32 字节 bus ID 长度由参考 USB/IP
实现交叉确认。下一步仍需取得真实云会话，把控制帧、标准 `OP_REQ_IMPORT` 和本地
VHCI 端口做一一对应验收。

## Step 16：压缩路径静态边界

继续检查 `UsbIpc.exe` 中 `support_compress_flag` 和
`compress_flag_threshold` 的引用，得到以下可以复核的事实：

- 配置读取代码把 `support_compress_flag` 写入全局开关，默认参数为 `1`；把
  `compress_flag_threshold` 写入全局阈值，默认参数为 `0x2800`（十进制 10240）。
- 发送/处理路径会先检查压缩开关，再检查消息结构首 DWORD 是否为 `0x10`，并要求
  偏移 `+0x18` 的长度不小于该阈值；条件不满足时走普通路径。
- USB 数据路径附近明确出现 `check_compress_header`、`deal_compress_submit`、
  `uncompre_ret_submit` 和长度越界日志，说明它在 `CMD_SUBMIT/RET_SUBMIT` 附近
  存在可选数据路径，而不是 USB/IP 标准 `DEVLIST/IMPORT` 控制面。
- `compressData`/`deCompressData` 这两个通用日志名还在网络打印机路径出现；同一
  二进制另有 miniLZO/LZMA 相关字符串。因此不能把外围设备的算法或包头直接套到
  USB 存储/HID 数据面。当前没有真实压缩帧、序号和解压输出可交叉验证，不能安全
  推断 USB 压缩包头字段、算法选择或长度语义。

Linux 适配因此保持“标准 USB/IP 字节透明转发 + 默认不启用厂商压缩”的边界。若
现场会话证明云端强制压缩，后续必须先采集同一设备的未压缩/压缩帧，确认包头、原始
长度、压缩长度、序号和失败行为，再加入算法白名单与严格上限；本轮不实现猜测性
解压器，也不把打印机/扫描仪/摄像头扩展带入 Debian 包。

## Step 8：Linux 第一版产物

已新增：

- `linux/src/ydyun_usbctl.py`：标准 USB/IP 用户态控制器。
- `linux/etc/ydyun-usb.conf`：远端地址、端口、允许的 bus ID 和重连参数。
- `linux/etc/systemd/ydyun-usbctl.service`：受限 systemd 服务。
- `linux/tests/test_usbctl.py`：配置、输入校验和 VHCI 输出解析测试。
- `debian/`：源码包元数据。
- `build-deb.sh`：无需 debhelper 的本地 `.deb` 构建脚本。
- `build/ydyun-usbctl_0.2.0-1_amd64.deb`：当前构建结果。

验证结果：

```text
python3 -m py_compile linux/src/ydyun_usbctl.py   PASS
16 Python unit tests                               PASS
dpkg-deb --info build/ydyun-usbctl_0.1.0-1_amd64.deb PASS
Debian trixie usbip --help                         PASS
```

当前机器已完成 root 环境下的 `modprobe`、systemd 单元校验和 `.deb` 安装；`usbip port`
当前为空。USB 云端会话尚未执行，仍需在实际云连接上验收。基础检查命令为：

```sh
modprobe usbip-core vhci-hcd
systemctl enable --now ydyun-usbctl.service
```

## Step 9：互通判断

Debian trixie `usbip` 工具支持 `--tcp-port PORT`、`list --remote HOST`、`attach --remote HOST --busid BUSID`，与当前控制器命令生成方式一致。官方包说明也明确 USB/IP 通过封装 USB request 在 IP 上转发，远端设备由本机原生驱动接管。

目前将 `3246` 视为 ZTE USB/IP 端口候选，将 `5100` 和 `19000` 视为控制/发现或外围设备扩展候选：原始字符串同时显示 `Nat_Port_In/Out_3246`、`Nat_Port_IN_UDP_5100`、`Nat_Port_IN_UDP_19000` 以及 `USBIP_REDIRECT_UNIFIED`。配置仍默认 `3240`，因为它是标准 USB/IP 端口；部署到 ZTE 云端时应在配置中显式改为实际会话提供的端口。

## Step 17：画面/桌面转发链路

这一步对 `vdesktop/VDesktop-setup-iceV7.25.43SP1.exe` 做了只读解包和静态分析。它与 `media` 包中的 DirectShow 音视频重定向不是同一条链路；ICE 包才包含整屏/桌面显示转发的核心。

### 组件分层

| 层 | 原包证据 | 作用判断 |
|---|---|---|
| 虚拟显示设备 | `ice.sys`/`icedd.dll`、`icedod.sys`、`iceidd.dll`、`ICEVGEmBus.sys` | 为 Windows 创建 ICE Display、Mirror Display 或 Indirect Display；旧版 XDDM 使用 `ICE_DISPLAY`/`ICE_DISPLAY_MIRROR`，新版 WDDM/IDD 使用 `IddCx0102`；不是 Linux USB 设备 |
| 画面捕获 | `IceVGPUCapture.exe` | 导入 `dxgi.dll`、`d3d11.dll`、`RapidFire64.dll`，出现 `IDDCapturer`、`OpenSharedResource`、`ICE_RECV_DISPLAY_CHANNEL_SURFACE_CREATE/STREAM_CREATE`，说明支持 DXGI/D3D11 桌面纹理/共享 surface 捕获 |
| 桌面编码 | `IceDisplay.exe`、`IceVGPUCapture.exe` | 出现 `zteH264Enc_Win64.dll`、`ice-encoder.dll`、`zte-lossless-encoder.dll`、`turbojpeg.dll`、`vpx.dll`，以及 H.264/HEVC、IDR、bitrate、dirty rectangle、frame cache 等字符串 |
| 传输隧道 | `IceTunnel.exe`、`ProtocolManager.dll` | `IceTunnel` 同时包含 TLS/OpenSSL、TCP、UDP、KCP、多路径和拥塞控制字符串；`ProtocolManager` 使用 `ICEProtocol`、display channel 和 virtual channel 相关接口 |
| 客户端输入 | `IceInput.exe`、`IceInputService.exe`、`IceCursorHook.dll` | 键盘、鼠标和光标是 ICE 独立输入通道，使用 HID/共享内存/输入会话，不是远端 USB 设备被本机 `vhci-hcd` 枚举 |

### 画面实际处理流程

当前静态证据支持的流程是：

```text
Windows 云桌面显示设备/物理 GPU
    -> IceDisplay 或 IceVGPUCapture
    -> DXGI/D3D11/IDD 共享 surface 或桌面帧
    -> GPU H.264/HEVC，或 ICE x264/AV视频编码器
    -> dirty rectangle / cache / zlib-zstd 无损区域合并
    -> ICEProtocol display channel
    -> IceTunnel 的 TLS + TCP/UDP/KCP 传输
    -> 远端客户端解码、合成并显示
```

更具体地说：

1. `IceVGPUCapture.exe` 的导入表和字符串明确包含 `dxgi.dll`、`d3d11.dll`、AMD AMF、NVIDIA NVENC、`IDDCapturer`、`CreateDXGIFactory1`、`OpenSharedResource` 和 H.264/HEVC 初始化；因此高性能路径是“桌面纹理捕获 + GPU 编码”，不是逐个读取 USB 显示设备。
2. `IceDisplay.exe` 内含 `cmd_encoder_*`、`desktop_stream.c`、`desktop_cache.c` 线索，配置键包括 `desktop-monitor-count`、`desktop-stream-rat`、`desktop-dve-quality`、`desktop-idr-interval`、`use-desktop-dupl`、`use-zip-lossless`、`multi-screen-video`。这表明它支持多屏、码率/质量动态调整、关键帧请求和可选无损区域。
3. `ice-encoder.dll` 的字符串显示 x264 风格编码参数、slice/NAL、CBR/VBV、QP、dirty/unchanged/text rectangle，以及 zlib/zstd 分段压缩；`zte-lossless-encoder.dll` 导出 `zte_lossless_encoder_*` 和 zlib/zstd 相关接口。这里的“无损”是桌面画面编码优化，不是 USB/IP 的 `compress_flag`。
4. `IceTunnel.exe` 出现 `send_idr_req_to_spice`、KCP ACK/RTT/窗口、UDP path、TCP sub path、TLS session 和带宽变化通知。可以确认它是带 TCP/UDP 传输与拥塞控制的专有隧道；不能仅凭字符串把它等同于公开 SPICE wire protocol。
5. `ProtocolManager.dll` 的 `GetVideoHandle`、`GetInputHandles`、`ICE_RECV_DISPLAY_CHANNEL_*` 和 `ICE_SEND_IDR` 字符串，说明会话控制、显示通道、视频帧、IDR 请求和输入通道是分开的消息/设备对象。

### 与媒体重定向的边界

`ZTERemoteRender.dll`/`ZTEVideoDmo.dll`/`ZTEAudioRenderer.dll` 主要拦截 Windows DirectShow 播放器，把 H.264/HEVC 等媒体样本送入 `MediaRedirectDll.dll` 的 TCP 重定向通道；`MMRHookService.exe` 还会注入目标进程。它是“播放器音视频加速/重定向”，不负责 Windows 桌面全屏采集。BCR 的 `RedirectAgent.exe`/`RedirectProxy.exe` 是浏览器页面内容重定向，也不是整屏协议。

### 对 Debian 适配的影响

- 当前 `ydyun-usbctl` 不应加入 ICE 图形协议；USB 存储/HID 仍走标准 USB/IP/VHCI，画面走独立的 `ydyun-ice` 方向。
- Linux 端若要复刻画面接收，需要真实云会话中的 ICE 会话握手、显示通道消息、TLS/KCP 参数、帧头、H.264/HEVC/无损区域封装和多屏合成数据；原包没有对应 Linux 客户端源码，静态字符串不足以安全伪造这些字段。
- Linux 接收端的实现基础可采用 PipeWire/DRM/KMS 或 DMA-BUF 作为显示输出，FFmpeg/GStreamer 作为 H.264/HEVC 解码器；但这只能替换本地显示/解码基础设施，不能替代尚未还原的 ICE 网络协议。
- 本机是物理 Debian 主机，不是云桌面 guest。不能把 `IceDisplayDriver`、`ice.sys`、`icedod.sys` 或 `ICEVGEmBus.sys` 安装到本机；这些是 Windows guest 侧虚拟显示/输入驱动。本机目前只验证 USB/IP 内核 VHCI，没有触碰物理键盘、鼠标、摄像头或蓝牙设备。

### 安全、监控和必须保留的协议安全

`VmQoEAgent`、`ice_qoe_log`、`Vmbooster` 等质量监控/代理不纳入 Linux 适配。`IceTunnel` 的 TLS、会话认证和密钥交换则属于画面协议本身的传输安全边界；如果云端强制要求它们，不能为了“去掉监控”而删除，否则会变成无法互通或明文传输。后续 Linux 版本只保留连接画面所需的最小认证/加密，不移植 QoE、进程守护、桌面监控和安全代理。

### 本步结论

1. “整屏画面转发”已确认是 ICE/RDS 专有协议栈：显示驱动/IDD 或 GPU 捕获 -> H.264/HEVC/无损桌面编码 -> ICE display channel -> TLS + TCP/UDP/KCP 隧道 -> 客户端解码显示。
2. 它与 USB/IP 存储、鼠标、键盘转发完全分离；当前 Debian USB 包的架构拆分是正确的。
3. 画面接收协议的帧级格式和会话握手尚未通过真实云会话验证，因此本轮不猜测实现、不把 Windows 驱动二进制塞进 Debian 包。下一阶段应先获取合法真实会话的控制/显示通道样本，再单独建立 `ydyun-ice` 原型。

## Step 18：画面转发的进一步证据与 Linux 实现边界

本轮继续从 ICE 二进制中提取带上下文的字符串和符号线索，并完成了一次不接触
物理 USB 设备的 Linux 虚拟设备验证。结论比 Step 17 更具体，但仍不等于已经还原
了 ICE 的线上协议。

### 画面通道的消息层次

`ProtocolManager-2008.dll` 和 `IceDisplay.exe` 同时出现以下成组消息名：

```text
ICE_SEND_DESKTOP
ICE_SEND_DESKTOP_ACK
ICE_RECV_DISPLAY_CHANNEL
ICE_RECV_DISPLAY_CHANNEL_INIT
ICE_RECV_DISPLAY_CHANNEL_SURFACE_CREATE
ICE_RECV_DISPLAY_CHANNEL_STREAM_CREATE
ICE_SEND_IDR
```

这组名称支持以下分层判断：

1. 会话先建立 display channel，然后发送 surface/stream 的创建与配置消息；
2. 画面帧不是单一裸 H.264 socket 流，而是被放入 ICE display channel 的消息体系；
3. `ICE_SEND_IDR` 是独立的关键帧请求/控制路径，客户端可以在丢帧、重连或首帧时要求
   服务端重新发送 IDR；
4. `GetVideoHandle` 与 `GetInputHandles` 分开出现，视频和键鼠输入不是同一个本地设备
   或同一类转发对象。

### 编码和增量更新

进一步的 `IceDisplay.exe` 字符串包含 `desktop_stream.c`、`desktop_cache.c`、
`cmd_encoder_*`、dirty rectangle、frame cache、IDR、lossless region 等调用线索，
并出现如下运行时参数：

```text
stream width / height
yuv_opt / cache / lossless / ziplevel / quality
desktop-stream-rat / desktop-dve-quality / desktop-idr-interval
use-zip-lossless / multi-screen-video
```

因此更接近真实行为的抽象是：

```text
屏幕 surface
  -> 脏矩形/缓存命中判断
  -> 有损视频帧（H.264/HEVC，必要时 AV1 能力协商）
     + 小区域无损块（zlib/zstd 或厂商 lossless encoder）
  -> stream/surface 元数据与帧消息
  -> display channel
```

这里的 `zlib/zstd` 是桌面画面增量更新的线索，不能与 USB `compress_flag` 路径混用。
USB 压缩阈值和 ICE 画面无损区域属于两套不同模块，Linux 适配必须分别处理。

### 传输层的进一步边界

`IceTunnel.exe` 中除了 OpenSSL/TLS 和 TCP/UDP 外，还出现 KCP 的 ACK、RTT、窗口、
拥塞控制、多 TCP path 和 `send_idr_req_to_spice`。这支持以下实现顺序：

1. 先确认真实会话是 TCP-only、UDP/KCP，还是 TLS-over-TCP 与 UDP/KCP 并行；
2. 再取得 ICE display channel 的握手和帧长度/序号字段；
3. 最后才在 Linux 端接入 FFmpeg/GStreamer 解码、DMA-BUF/PipeWire/SDL 显示。

仅凭二进制字符串不能安全决定 KCP 会话号、认证数据、TLS 私有扩展或帧头布局，不能
用一个“看起来像 H.264”的裸流接收器替代它。

### Linux 端的可实施拆分

后续实现应保持两个独立包/进程：

| 组件 | 负责内容 | 当前状态 |
|---|---|---|
| `ydyun-usbctl` | USB/IP 控制、VHCI 导入、存储/HID 类驱动衔接 | 已有首版，真实云会话待验收 |
| `ydyun-ice` | ICE 会话、display channel、视频/无损块解码、光标和输入 | 仅完成静态协议边界，尚未实现线上握手 |

`ydyun-ice` 不应安装 Windows `ice.sys`、`icedod.sys`、`iceidd.dll` 或
`ICEVGEmBus.sys`。它应是用户态协议客户端；Linux 显示输出使用系统已有的显示栈，
输入则单独选择 evdev/uinput 或协议自身的输入通道。TLS/认证如果是会话必需部分必须
保留；QoE、进程监控、远程诊断、网络拦截和安全代理不应移植。

### 本轮虚拟 USB 验证边界

为验证 Linux 原生类驱动链路，使用 `dummy_hcd + g_mass_storage` 创建了仅存在于内核
中的虚拟存储设备。内核日志确认 `usb-storage` 识别设备并创建 SCSI host；该结果证明
USB 类驱动和 VHCI 方向可以由 Linux 原生组件承接，但不是云端互通证明。

随后使用 `usbip-vudc + g_mass_storage + usbipd` 做本机控制面实验：服务端收到了并接受
`OP_REQ_IMPORT`，但 vUDC 数据通道以 `usbip_vudc: v_rx exit with error -32` 结束，客户端
导入失败。测试镜像、模块和服务进程已清理，`usbip port` 恢复为空；没有解绑或导出本机
物理键盘、鼠标、摄像头和蓝牙设备。因此这次实验记录为“控制面可达、数据面未闭环”，不
把它写成完整 USB/IP 通过。

### Step 18 结论

画面转发已经从“可能是桌面视频”收敛为“surface/stream 控制消息 + H.264/HEVC/无损
增量编码 + ICE display channel + TLS/TCP/UDP/KCP 隧道 + 独立输入/光标通道”。还缺的
是合法真实云会话中的认证、通道、帧头、序号和编码块样本；在取得这些样本前，不加入
猜测性的 `ydyun-ice` 代码，也不将 ICE Windows 驱动安装到物理 Debian 主机。

## Step 22：标准 USB/IP 虚拟端到端验证

为验证 Linux 适配器不只是“能生成 usbip 命令”，在本机创建了隔离的 `dummy_hcd` 虚拟
USB 总线和 `g_mass_storage` 虚拟存储设备，并由发行版 `usbipd` 在本机 3240 端口导出。
验证过程使用已安装的 `ydyun-usbctl` 完成：

1. `probe` 收到标准 USB/IP `0x0111`、1 个设备；
2. `list` 识别虚拟 busid `21-1`；
3. root 下 `attach 21-1` 成功导入 VHCI `Port 00`；
4. Linux `usb-storage` 创建 `/dev/sda`；
5. `/dev/sda` 首个 4096 字节与虚拟远端 backing file 的 SHA-256 均为
   `ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7`。

这证明标准 USB/IP 数据面、VHCI 和 Linux 存储类驱动可以完整衔接。该实验没有解绑或
导出物理键盘、鼠标、摄像头、蓝牙，完成后已 detach、停止 usbipd、卸载虚拟模块并删除
测试镜像。它仍不能替代真实 ZTE 云会话对 3246 私有控制面、压缩和重连行为的验证。

## Step 19：补查 uSmartPlayer 客户端与整屏转发的边界

在原始目录中还发现并只读解包了 `vdesktop/VDesktop-setup-uSmartPlayer.exe`。它的
安装包确实出现 `ICE`、`KCP`、`TLS`、`UDP`、`USB` 等字符串，但进一步检查其实际
主程序 `uSmartPlayer.exe` 后，可以把它与整屏 ICE 接收器区分开。

### uSmartPlayer 实际功能

`uSmartPlayer.exe` 是 Qt/FFmpeg/mpv 媒体重定向播放器，不是完整的远程桌面显示客户端：

- 导入 `avcodec-58.dll`、`avformat-58.dll`、`avutil-56.dll` 和 Qt Network；
- 启动随包提供的 `player/mpv.exe`，使用 `\\.\pipe\mpvsocket` 做本地播放器控制；
- `FileStream::buildVideoCreateMsg` 的日志字段包含 `codec_type`、时间戳、流/源尺寸、
  `bits_per_coded_sample`、B 帧和像素格式，说明它处理的是媒体样本流；
- `DataSender`/`DataReceiver` 使用 Qt `QTcpSocket`，另有 `TcpFileServer` 与本机
  `vdagent` 连接；
- `buildUuidVerificaionMsg`、`CLIENT_COMPATIBLE/IS_PEER_COMPATIBLE` 和 `Ice-UUID` 是
  媒体重定向的会话校验线索；
- `SpiceClipRedirectRects`、`MediaRedData`、`onPlayVideoData` 与 `getTopWindows` 也
  指向播放器/媒体重定向，不等同于 `IceDisplay` 的整屏 surface/stream 通道。

随包的 `mpv.exe` 虽然包含 H.264、HEVC、AV1、RTP 等通用解码能力，但这些是播放器
能力，不能证明 ICE 桌面帧就是 RTP，也不能直接套用到 ICE display channel。

### 安全组件边界

同一 `uSmartPlayer` 安装包中还带有 `DriverController.dll`。其字符串包含
`IOCTL_NTPROCDRV_ENABLE_PROC_LIST`、进程黑名单和 Windows 服务安装/启动/删除逻辑，
属于进程控制/安全相关组件；Linux 适配不应移植它。媒体播放所需的 TLS/UUID 校验与
进程黑名单是不同边界，前者如果协议要求仍需保留，后者排除。

### 对画面适配的新增结论

1. 原始资料同时包含 guest 侧 ICE server/display 组件和一个媒体重定向播放器，但没有
   发现可直接复用的 Linux/Windows 完整远程桌面 viewer 主程序。
2. `uSmartPlayer` 可以帮助后续理解媒体重定向的 TCP/UUID/FFmpeg 数据结构，但不能
   作为整屏画面接收端；整屏仍必须以 `IceDisplay`/`ProtocolManager` 的 display
   channel 为目标。
3. Linux 端若需要同时支持“整屏”和“媒体重定向”，应拆成 `ydyun-ice` 与独立媒体
   组件；不能把播放器的 H.264/RTP 解析器冒充整屏 ICE 协议。

这一步让画面、媒体、USB 和安全组件的边界更加明确，避免把安装包中看似相关的
`uSmartPlayer` 错当成远程桌面 viewer。

## Step 20：确认 ICE 使用标准 SPICE framing，并加入离线解析基础

对 `IceTunnel.exe`、`IceDisplay.exe` 和 `IceInput.exe` 的反汇编进行常量核对后，发现
三个组件都直接比较或写入 `0x51444552`。在小端机器上该值编码为 ASCII `REDQ`，与
SPICE `SPICE_MAGIC` 完全一致，而不是仅仅因为日志里出现了 `SPICE` 这个词。

进一步证据如下：

- `IceTunnel` 检查 `header.magic`、`major_version`、`size`，并枚举标准主、显示、
  输入、光标、音频和 USB 重定向通道；
- `IceTunnel` 对标准 link header 的主版本要求为 `2`，并拒绝大于 `0x2800` 的 link
  message；解析器按这一已确认的上限处理 link header，数据帧则使用独立的 64 MiB
  安全上限；
- `IceDisplay` 处理 `tn_parse_spice_msg`、`tn_deal_channel_spice_msg`，并检查
  `SPICE_CHANNEL_MAIN` 与 `spice_header.size`；
- `IceTunnel` 具有 `tn_parse_display_stream_data` 和 `spice_header_allow_drop`，说明
  display stream 在标准 SPICE 消息外有厂商自己的低延迟/丢帧策略；
- Debian `libspice-protocol-dev` 的公开头文件验证了 `SpiceLinkHeader`、
  `SpiceDataHeader`、通道号以及 `SPICE_MSG_DISPLAY_STREAM_*`/输入/光标消息编号。

据此新增：

- `linux/src/ydyun_spice.py`：严格限长的 SPICE link/data/mini-data header 解析器；
- `linux/src/ydyun_ice_probe.py`：只处理用户提供的离线字节，不联网、不解密、不抓包；
- `linux/tests/test_spice.py`：magic、标准头、增量输入、mini header、超长帧和坏包测试。

这一步把 Linux 画面适配从“等待未知专有帧格式”推进到“标准 SPICE 消息层已具备解析
基础，剩余重点转为 ICE 隧道认证、TLS/KCP、多路径和厂商扩展”。仍不能据此宣称真实
云端画面已经可以显示；探针也不会伪造登录或绕过协议认证。

## Step 21：补齐标准 display stream 元数据边界

从 Debian `spice` 0.15.2 源码中的 `spice.proto` 和生成结构确认了标准 display stream
消息的字段布局：`STREAM_CREATE` 的固定部分为 50 字节，随后是 `SpiceClip`；其中明确
包含 surface ID、stream ID、编码枚举、时间戳、源/目标尺寸。`STREAM_DATA` 的固定部分
为 12 字节，后接编码样本长度和样本；`STREAM_DATA_SIZED` 的固定部分为 36 字节，额外
携带样本尺寸和目标矩形。

已将这些确定字段实现为 `decode_display_message()`，并让 `ydyun-ice-probe` 输出 codec、
stream ID、媒体时间、尺寸和 encoded byte count。新增两个测试覆盖 H.264 stream-create
和编码样本边界。该实现只做结构化观察，不解码、不绕过 TLS/KCP、不把 ZTE 扩展猜成公开
SPICE 字段，因此可以安全用于后续真实会话样本分析。

## Step 23：键盘和鼠标 HID 端到端验证

在同一隔离的 `dummy_hcd` 虚拟总线上使用 configfs 创建了一个复合 HID gadget：接口 0
是 boot keyboard，接口 1 是 boot mouse。设备先在本机由 `usbhid` 枚举，再绑定
`usbip-host`，由本机 `usbipd` 导出；已安装的 `ydyun-usbctl` 通过标准 `probe/list/attach`
导入到 VHCI。

导入端确认：

- `vhci-hcd` 枚举出两个 HID interface；
- `usbhid`/`hid-generic` 创建键盘和鼠标 evdev 节点；
- 从 gadget 的 `/dev/hidg0` 写入键盘按键/释放报告后，evdev 收到 `EV_KEY` 扫描码；
- 从 `/dev/hidg1` 写入鼠标报告后，evdev 收到 `BTN_LEFT` 和 `REL_X` 事件。

这与存储端到端测试一起证明 Linux 原生 `usb-storage`、`usbhid` 和 VHCI 数据面可以承接
标准 USB/IP 导入。测试只使用虚拟 gadget，结束后已 detach、停止 `usbipd`、卸载虚拟
模块并删除 configfs gadget；未解绑任何物理设备。真实 ZTE 云会话仍需验证其私有 3246
前置控制、压缩和重连语义。

## Step 24：中国移动云电脑官方 Linux/Windows 客户端反向验证

从中国移动云电脑官方桌面下载页取得了 Windows、UOS AMD64、麒麟 AMD64 三个客户端包，并保存到 `doc/client-packages/`；对应 SHA-256、静态解包位置和详细证据见 `doc/CLIENT-ANALYSIS.md`。包均只做静态分析，没有执行客户端或安装脚本。

官方 Linux 客户端把功能分为 Electron 管理层、base 云桌面原生 SDK、ZTE 原生 SDK 和 USB/Spice 原生库。`chuanyun-redirect` 依赖 `libchuanyun_usbip.so` 与 `libusbipd.so`，包含标准 USB/IP DEVLIST/IMPORT、libusb、udev、热插拔和设备映射符号；这与本项目采用 Linux `usbip-core`/`vhci-hcd`、`usb-storage`、`usbhid` 的路线一致。ZTE 版本的 `usbredirect` 则在此基础上增加 Spice 命令触发、设备策略、压缩、摄像头/扫描仪及日志追踪，不能把它直接作为干净 Debian 核心依赖。

画面链路获得了更直接的 Linux 证据：`chuanyun-view` 依赖标准 `libspice-client-glib-2.0`，`uSmartView_VDI_Client` 依赖厂商版 `libspice-client-glib-zte-2.0`，并出现 `spice_main_channel_update_display`、`spice_gtk_get_display_frame_param`、`spice_gtk_get_display_rgb_data`、`spice_gtk_key_down`、`spice_gtk_mouse_motion` 等符号。因此画面是原生 Spice/VDI display channel，键鼠控制输入是 Spice input channel，均不是 USB/IP 数据面。

官方包还包含 `QoEAgent`、`qoe.service`、监控脚本、进程守护配置、性能/进程/打开文件采集脚本、probe/日志上报和 `libzxsecurity`/`libusbtrace`/`libqoelog` 等组件。本项目将这些归类为安全、监控、遥测或运维功能，最终 Debian 包继续保持排除；只保留标准 USB/IP 控制器和已验证的显式协议边界。

本次反向验证增强了 USB 和画面架构判断，但没有替代真实云会话：3246 私有控制面、Spice/ICE 的 TLS/KCP/厂商扩展、认证令牌及云端多设备配对仍需真实会话样本。当前 `ydyun-ice-probe` 仍是离线 SPICE framing/display metadata 探针，不宣称已经完成云端画面显示。

## Step 25：补齐本地 USB 上行导出方向

重新核对官方 Linux `chuanyun-redirect` 的符号和 Linux USB/IP 行为后，确认适配器必须同时考虑两种方向：

```text
云端导出 -> ydyun-usbctl attach -> vhci-hcd -> 本机 usb-storage/usbhid
本机 USB -> usbip-host -> usbipd/云端 peer
```

此前已有的 `attach/list/port` 只覆盖第一条方向。本轮新增：

- `[local_devices]` 配置段；
- `local-list` 只读枚举本机 USB；
- `export`/`unexport` 单设备操作；
- `export-watch` 和默认禁用的 `ydyun-usb-export.service`；
- 导出前只对明确选中的 bus ID 解绑当前接口驱动，成功后交给 `usbip-host`；
- `usbip unbind` 后写入 `/sys/bus/usb/drivers_probe`，让 `usb-storage`/`usbhid` 重新接管。

为什么需要解绑：本机存储接口通常已经由 `usb-storage` 绑定，本机键鼠接口已经由
`usbhid`/HID 驱动绑定，USB/IP host 不能同时占有同一接口。官方 `usbredirect` 的
libusb 路径也必须处理这个占用关系。

验证使用完全隔离的 `dummy_hcd + g_mass_storage`，没有触碰物理设备：虚拟 bus ID
`21-1` 被控制器自动从 `usb-storage` 释放，`usbip-host` 成功导出，`usbipd` 返回
DEVLIST，随后标准 `usbip attach` 成功创建 VHCI Port 00；清理时执行 unexport、停止
usbipd、卸载虚拟模块并删除测试镜像，最后 `usbip port` 为空。

这一功能是标准 USB/IP 上行能力，不等于已经通过中国移动云端的私有 Spice/ZTE 会话；
真实云端仍可能要求 3246 前置控制或厂商 `importDev` 消息。导出实体键盘/鼠标期间，
本机相应输入会暂时不可用，因此默认服务和配置均不自动接管任何物理设备。

## Step 26：用官方 Linux SDK 符号确认画面与输入边界

对官方 UOS/麒麟包中的 ELF 和 Windows SDK 头文件做了最后一轮静态交叉核对，结果与
前面的 SPICE 分析一致：

- Linux `libchuanyun.so` 导出 `connectVm` 等会话入口；`libchuanyun_usbip.so` 导出
  `cdp_usbip_start_jwae_service`、令牌/用户名密码设置等 USB 服务接口。也就是说，
  登录与 USB redirect 的会话编排在厂商 SDK 内，不能从标准 USB/IP 数据包本身推导出
  完整认证流程；
- 官方 Linux `libspice-client-glib` 导出 `connection_connect_set_config`、
  `connection_connect_set_session_id`、`connection_connect_session`、
  `spice_session_connect`，以及 display stream、键盘按键、鼠标按钮和坐标输入函数；
- `chuanyun-view` 仍保留 `qspice-display-channel.cpp`、`qspice-inputs-channel.cpp`
  等调试符号，能看到 `display_handle_stream_create` 和
  `display_handle_stream_data`，进一步确认画面接收是独立的 display channel；
- 官方 Windows `RemoteDesktopSDK.h` 把 `startUsbRedirect`/`stopUsbRedirect` 与
  `getDesktopStreamParam`/`getUpScreenStreamParam`、`FrameInfoCb` 分开定义，说明
  USB 重定向、桌面画面和本地输入是三个不同的 SDK 边界，而不是一个 USB 驱动协议。

因此当前 Linux 适配的职责边界已经固定：USB 设备使用标准 `usbip-host`/`vhci-hcd`，
本地键鼠控制云桌面使用独立的 Spice input 通道，画面使用独立的 Spice/厂商 display
stream 接收器。没有真实账号会话、令牌和加密前后的帧样本时，不伪造 `connectVm` 私有
协议，也不把猜测性的 viewer 放进可安装驱动包；`ydyun-ice-probe` 继续只分析用户提供
的离线字节。

## Step 27：确认官方 Linux 客户端的会话入口不是裸 SPICE

从官方 Electron 源码、请求封装和未剥离的 `chuanyun-view` 调试符号补齐了实际调用边界：

1. 普通云桌面先访问 `/terminal/cc/getFirmAuth/v1`，成功响应的 `d.data` 包含 VM、VMC、
   CAG 和可选 SCG 参数；
2. Electron 将这些参数送入 `zteWorker.connect` 或 base worker，而不是直接连接标准
   USB/IP 端口；
3. `QSpiceConn::StartConnection` 依次建立 JWAE/SCG、设置 token、序列化
   `ConnectionConfig`，设置 session ID，然后调用 vendor SPICE connection；
4. `ConnectionConfig` 已静态确认包含 common/peripheral、视频编码和网络自适应配置，
   其中有 `usbPortRedirectEnabled` 和 `networkTransportProtocol` 等字段。

同时确认官方 REST 请求使用 RSA 无填充分块加密业务 body 和 HMAC-SHA256 请求签名，且
接口路径、厂商连接参数和 SCG token 都会随登录会话变化。由此排除了“仅凭 3246 或 3240
端口探测就能完成画面/USB 云端接入”的错误路线。后续真实适配要做的是合法会话参数
适配和协议接收，不是绕过认证；监控、QoE、遥测和安全代理仍不进入 Linux 包。

## Step 28：把标准 display stream 推进到离线样本提取

在不执行官方 Linux ELF、不开启网络连接的前提下，将 `ydyun-ice-probe` 从元数据探针
扩展为标准 display stream 样本提取器：

- `STREAM_DATA` 和 `STREAM_DATA_SIZED` 依据已确认的固定前缀和长度字段提取编码样本；
- `--extract-dir DIR` 将样本保存为 `stream-<id>-<codec>-<序号>.es`，目录由操作者显式
  指定，不使用 `/tmp`；
- JSON 行同时报告样本文件路径，便于随后用 Debian 的 FFmpeg/ffprobe 验证 H.264/HEVC
  解码能力；
- 提取器不处理 TLS/KCP、JWAE/SCG、ZTE 私有帧头或无损区域，也不会把未知字段当成
  标准 SPICE 数据。

这使画面适配可以按“合法会话解出字节 -> 离线确认 framing -> 提取编码样本 -> 解码/合成”
分阶段验证，同时继续把认证和厂商扩展留在独立的 `ydyun-ice` 工作项中。当前仍没有
真实云会话样本，因此不能把离线提取器称为完整画面客户端。

## Step 29：编译验证 Debian 原生 SPICE 显示/输入边界

在本机 Debian trixie 上安装发行版 `spice-gtk` 开发包后，新增并编译了
`linux/experimental/ydyun_spice_viewer.c`：

- 使用公开的 `SpiceSession`、`channel-new` 和 `spice_display_new()` API；
- 将标准 display channel 挂到 GTK 窗口，标准 SPICE input 由 `SpiceDisplay` 接收；
- 通过 `cc -Wall -Wextra -Werror` 编译为 x86_64 ELF，并确认动态链接到发行版
  `libspice-client-gtk-3.0`/`libspice-client-glib-2.0`；
- 该 harness 只用于标准 SPICE endpoint 的下层验证，不进入生产 `.deb`，不执行官方
  `chuanyun-view`、不调用 JWAE/SCG、不处理登录令牌，也不包含 QoE/监控/安全组件。

这一步证明 Linux 画面显示和本地输入的通用接收层可以独立编译；是否能连接中国移动云
端仍取决于合法会话提供的 JWAE/SCG 隧道、厂商连接 JSON、session ID 和扩展 framing，
不能用标准 SPICE harness 代替。

## Step 31：补齐官方 ZTE 会话的 IPv6 参数

继续检查官方 Electron 主进程和 `zteWorker.connect` 调用，确认 ZTE 路径除了
`vmcIp/vmcPort`、`cagIp/cagPort` 外，还会按条件传递 `cagIpv6`、`scgIp`、
`scgTcpPort`、`scgUdpPort`。`ydyun-session-probe` 现已解析并脱敏保存 `cagIpv6`，
并在没有 IPv4 CAG 地址时使用 IPv6 CAG 地址计算完整路由数；新增 IPv6 回归测试覆盖该
情况。该工具仍只处理操作者已经合法取得的 JSON，不建立网络连接。

## Step 30：增加本地 USB 上行的 Hub 安全边界

对本地 `export` 路径增加了 sysfs `bDeviceClass` 检查：USB hub/root hub 类设备在任何
模块加载、接口解绑和 `usbip-host bind` 之前直接拒绝；普通 USB storage、HID 键盘和
鼠标仍按显式 `[local_devices]` 配置处理。新增回归测试确认拒绝发生在加载导出模块之前，
从而不会因为配置错误而影响整条物理 USB 总线。

## Step 32：确认 `libjwae` 不能仅凭导出符号安全复用

官方 Linux 包的 `libjwae.so` 确实导出了 JWAE/SCG 入口，但只有动态符号和一个
`.gnu_debuglink` 名称，没有随包提供的头文件或调试类型定义。静态分析能够确认调用
边界名称，不能确认参数布局、回调函数签名、线程/内存所有权和版本兼容性。由此将
`libjwae` 明确列为闭源会话适配边界，不在 Debian USB 驱动中加入猜测 ABI、`ctypes`
调用或官方二进制复制；标准 USB/IP 和公开 SPICE 下层仍保持可独立验证。

## Step 34：按字段区分画面/USB核心配置与监控配置

继续对官方 `chuanyun-view` 的 DWARF 类型和字符串做静态核对，确认
`ConnectionConfig` 包含系统/SDK 元数据、编码阈值、视频编码参数和网络自适应配置；
`CommonConfig` 还包含 `usbPortRedirectEnabled`、`networkTransportProtocol`，以及
`sdkPointCollectPeriod`、`sdkPointReportPeriod`、`sdkPointEnbaled`、
`publicProbeTimeout`、`publicProbeDomain` 等字段。

因此“去掉安全和监控”不能粗暴删除所有网络或安全字段：USB 转发开关、会话传输策略
和画面所需 TLS/认证仍是功能边界；SDK 质量采集、公网探测、摄像头/磁盘映射、剪贴板
和文件拖放属于当前目标之外的外围能力，均不进入 Debian 包。字段级证据和完整布局
已记录在 `doc/CLIENT-ABI.md`。

## Step 33：从官方 SPICE 库 DWARF 还原连接对象边界

对官方麒麟 AMD64 客户端的 `libspice-client-glib-2.0.so.8.8.2` 做了只读 DWARF
解析。该库带有调试类型信息，因此比动态符号提供了更强的静态证据：

- `connection_new(void)` 返回 `SpiceConnection *`；
- `connection_connect_set_config(SpiceConnection *, const char *)` 和
  `connection_connect_set_session_id(SpiceConnection *, const char *)` 均返回 `int`，
  两个配置参数都是 NUL 结尾字符串；
- `connection_connect_client(SpiceConnection *)`、
  `connection_connect_session(SpiceConnection *)` 均返回 `int`；
- 官方析构入口名称确认为拼写错误的 `connection_destory(SpiceConnection *)`；
- `_SpiceConnection` 的 DWARF 大小为 104 字节，含 `GObject`、`SpiceSession`、
  `GMainLoop`、主通道、状态字段、JSON 配置指针、session ID 指针和参数列表。

这进一步证明 Linux 客户端的画面入口是“已取得会话 JSON/session ID 后，由用户态
连接对象建立厂商 SPICE 会话”，而不是 Windows 驱动或裸 USB/IP 端口。它为未来的
最小画面适配提供了明确的静态 ABI 边界，但没有证明 JSON 字段语义、JWAE/SCG 握手、
TLS/KCP 扩展或真实帧格式。因此未执行该 ELF、未猜测 `ctypes` 调用，也未将闭源库
加入 Debian 生产包；完整证据单独记录在 `doc/CLIENT-ABI.md`。

## Step 35：还原官方 USB LZ4 封装并保持默认关闭

对官方麒麟 `libusbipd.so` 做了只读 DWARF/反汇编分析。`network_send_data_with_compressed`
和对应接收函数使用 `type:u8 + uncompressed_size:u32 + compressed_size:u32` 的 9 字节
网络头；`type=0` 为原文，`type=1` 为 raw LZ4 block。短于等于 512 字节或压缩无收益时，
官方发送器直接发 type-0；无数据则走未封装路径。

Linux 反向桥现在提供显式 `backward.compression=off|lz4`。`off` 仍是默认值并保持
透明转发；`lz4` 只做经过长度限制和 LZ4 边界检查的封装转换，不处理认证、TLS/KCP 或
未知厂商命令。该实现提高了与官方用户态 USB/IP 服务的可验证兼容性，但没有把静态
证据冒充真实云端互通，真实会话仍需确认封装是否确实启用。

进一步反汇编确认官方 `network_send_data()` 与 `network_recv_data()` 在连接对象的
压缩开关偏移 `+0xe30` 上分支；USB/IP 的 `send_xfer_data`、DEVLIST/IMPORT 等调用
通过这些入口发送或接收。因此 LZ4 是 USB/IP 字节流的外层包装，而不是替换 USB/IP
协议。Linux 桥按同一边界转换，仍把 0x210 私有控制帧、认证和 TLS/KCP 保持在转换器
之外。

## Step 36：画面与键鼠的最终分层证据

对官方麒麟 `chuanyun-view` 和 `libspice-client-glib-2.0.so.8.8.2` 的动态符号、字符串
和回调名称做了只读核对。画面侧存在 `displayPrimaryCreate/Invalidate/Destroy`、
OpenGL invalidate、cursor 和 display mark 回调；输入侧存在
`spice_inputs_channel_position`、button press/release、key press/release 及 key-lock
状态回调。

因此最终架构明确分成两条链：

```text
JWAE/SCG + session JSON
        -> vendor SPICE connection
        -> display channel -> surface/stream -> H.264/HEVC/无损区域 -> GTK/Wayland
        -> inputs channel <- Linux GUI mouse/keyboard events

USB channel -> USB/IP/VHCI -> usb-storage/usbhid -> USB storage/HID
```

当前 Debian 包已经完成后一条 USB 链和公开 SPICE 下层验证；真实云端认证、厂商
SPICE channel 和 ICE 扩展仍是必须通过实际会话验证的部分，没有被静态符号冒充完成。

## Step 37：官方 Linux USB 主链路不是 3246 监听端口

继续对官方麒麟 `libusbipd.so` 的 DWARF、局部符号和反汇编做了只读还原，确认了
官方客户端 USB 云端路径的关键调用图：

```text
JWAE/SCG 提供的 g_link
    -> handle_usbip_link_init
       -> 发送 USB/IP common header: version=0x0111, code=0x8008, status=0
       -> 接收 code=0x0008/status=0
    -> handle_usbip_command
       -> 0x8005 OP_REQ_DEVLIST -> 0x0005 OP_REP_DEVLIST
       -> 0x8009 OP_PRE_REQ_IMPORT(busid[32])
          -> 标准 0x0003 OP_REP_IMPORT + 设备描述
          -> 接收标准 0x0003 请求确认
          -> forwarding_start/libusb 打开并转发本机设备
```

这里的 `g_link` 是 JWAE/SCG 内部建立的网络对象；`network_send_data`/`network_recv_data`
在其上层执行可选的 raw/LZ4 封装。它不是本机 TCP 3246 监听 socket，也不是可以脱离
认证、TLS/KCP 和会话配置独立拨号的公开 USB/IP 服务。因此当前 3246 反向桥仍只能
覆盖已知的旧式/私有入口，不能冒充官方 Linux 客户端的主链路。

同时确认 `android_usbipd_start(json,on_init)` 的职责不同：它启动的是本机 RemoteHub
USB 服务，初始化配置、libusb hotplug、设备列表及本地导出；云端主链路由另一个
`usbip_rx_handler` 在已建立的 `g_link` 上处理。Linux 精简版只保留标准 USB/IP/VHCI
和必要的显式桥接，不移植 `deny_dev`、热插拔策略、守护/监控或未经授权的厂商服务。

结论：USB 数据面的标准协议和本机设备转发已经可验证；要完成“官方云电脑会话直连”，
还需要合法取得真实 JWAE/SCG 会话参数或抓包样本，再实现明确的主链路适配，不能根据
闭源对象偏移猜测一个生产连接器。

## Step 38：画面控制面与画面数据面再次分离

对官方 Linux `libchuanyun.so` 的导出符号和 `.rodata` 做了只读核对，发现
`RDPOperatorClass` 暴露 `GetDesktopStreamParam()`、`GetUpScreenStreamParam()`，并在
同一控制对象中出现 `SendDesktopStream`、`SendUpScreenStream`、`renderFirstFrame` 和
`SendMonitorIndex` 字符串。这说明会话建立后至少存在“获取上下行画面流参数”和“发送
画面流控制/上行画面”的控制面调用；它不能证明这些字符串就是视频帧格式。

实际画面数据仍由 vendor SPICE/ICE channel 处理：显示 surface、脏区、cursor 和编码
帧在该 channel 中接收/解码，`libavcodec`、OpenH264、x264/x265、dav1d 等库只是官方
客户端的编解码候选依赖。Linux 适配不能只实现 HTTP/JSON 的 stream 参数请求就声称
完成画面转发，也不能把 `SendMonitorIndex` 这类质量采集入口带入精简包。

因此画面适配的实现顺序固定为：先用真实会话确认 stream 参数和 vendor channel 的
封装，再复用 Debian 的 FFmpeg/硬件解码和 GTK/Wayland 显示；USB storage/HID 仍走
独立 USB/IP/VHCI 通道。当前已完成静态分层和公开 SPICE harness 验证，真实画面帧仍
标记为待会话样本验证。

## Step 39：从官方头文件确认 Linux 会话适配 ABI

在已下载的官方麒麟客户端中找到 `ccsdk/uos/include/chuanyun_api.h`，确认 Linux
后端有明确的 C ABI，而不是只能通过 C++/ctypes 猜测：`chuanyun_init` 接受服务和
回调结构，`connectVm` 使用 `vmid/username/auth_code/biz_code`，`disconnect` 按 VM ID
断开，`deInit` 释放 SDK 资源。首帧回调独立于连接状态回调，说明画面首帧是会话建立后
由 vendor viewer/Spice channel 完成的异步阶段。

据此新增 `ydyun-chuanyun-session` 可选 loader。它使用 POSIX `dlopen`，只在用户明确
执行时加载外部 `libchuanyun.so`；默认 Debian 服务不加载它，不传入日志服务器、网络
状态或事件监控回调，也不输出认证材料。这样既能在取得合法真实会话后做 ABI 级验证，
又不会把官方闭源库、安全代理或监控路径伪装成已完成的开源 Linux 驱动。

## Step 40：还原官方 JWAE `StartConfig` 与画面/USB 分流

对官方 UOS `chuanyun-view` 的 DWARF 和 `QSpiceConn::startSCG()` 做了只读分析，确认 `StartConfig` 为 112 字节，字段顺序为 `mode`、`auth_type`、TCP/UDP IPv4/IPv6 地址、client 地址、五个端口、`config_path`、`ping_ip` 和 `SdkLogConfig`。viewer 把 `auth_type=2` 写入结构，随后调用 `jwae_set_token()` 和 `jwae_start()`；这比只看 `libjwae.so` 导出符号更接近真实调用边界。

该证据进一步固定画面路径：

```text
授权 getFirmAuth -> JWAE/SCG StartConfig/token -> vendor SPICE/ICE
    -> display surface/stream -> H.264/HEVC/区域帧解码 -> Linux 窗口
    -> inputs channel <- Linux 鼠标/键盘事件
```

USB 路径则在同一个 JWAE/SCG `g_link` 上独立执行 USB/IP 握手、DEVLIST、PRE_IMPORT 和标准 IMPORT，再交给 Linux `usbip-core/vhci-hcd/usb-storage/usbhid`。因此画面转发不是 USB 模拟，桌面键鼠也不等同于物理 USB HID 转发。

`SdkLogConfig` 和 `ConnInfoNew` 是日志/网络质量与 cloud-peripheral 监控相关结构，没有纳入精简适配器；当前 Debian 包仍不携带 `libjwae.so`、官方监控模块或闭源安全组件。该步完成的是静态 ABI 与架构确认，不等于已获得云端真实会话；真实首帧、USB 压缩开关和断线行为仍待授权会话样本验证。

## Step 41：正式 Debian 包运行时边界审计

对 `build/ydyun-usbctl_0.2.23-1_amd64.deb` 做最终审计：包的运行时依赖仅为
`python3` 和发行版 `usbip`；两个 C ABI loader 只依赖 `libc.so.6`，不链接官方
`libjwae`、Qt、Electron、libusbipd 或监控库。包内没有 `.exe/.dll/.sys/.pdb`，也没有
`WinDivert`、Guard、QoE、telemetry、trace 或 monitor agent 文件。

三个 systemd 单元通过 Debian 规则固定为 `dh_installsystemd --no-enable --no-start`，
安装不会自动创建开机启用链接或启动 USB 接管。`ydyun-usbctl doctor` 只读取架构、
模块、VHCI 和本地枚举状态；最终安装后实测三个单元均为 `disabled/inactive`，VHCI
导入列表为空。

因此正式包的实际边界是：

```text
ydyun-usbctl -> usbip-core/vhci-hcd -> usb-storage/usbhid
```

标准 SPICE/ICE 解析器和官方 ABI loader是显式的兼容/离线工具，不会自动启动、登录、
采集监控数据或接管安全策略。真实云端 JWAE/SCG 会话仍需授权样本验证，不能由本次包
审计替代。
