# Windows 客户端驱动层分析

本文件记录从官方 Windows 客户端安装包中提取的驱动静态证据。分析对象是驱动安装
文件、PE 依赖和字符串，不在本机加载 Windows 驱动，也不把它们复制进 Debian 包。

## `cdp_usb.inf`：虚拟 USB 设备的 libusbK 绑定

官方 Windows 客户端的
`chuanyun-view/bin/drivers/cdp_usb/cdp_usb.inf` 给出了明确的安装边界：

| 项目 | 证据 |
|---|---|
| 硬件 ID | `USB\VID_28EE&PID_8551` |
| 设备类 | `CDP Remote USB` |
| 驱动服务 | `libusbK` |
| 服务类型 | `SERVICE_KERNEL_DRIVER` (`1`) |
| 启动类型 | `3`，按需启动，不是系统启动常驻服务 |
| 服务文件 | `%12%\libusbK.sys` |
| 驱动版本 | `3.1.0.0`，INF `DriverVer=2024-08-23, 3.1.0.1` |
| WDF | `KmdfLibraryVersion=1.11` |

这说明 `cdp_usb.inf` 绑定的是一个具有固定 VID/PID 的 CDP 虚拟 USB function，
通过通用 `libusbK.sys` 暴露 USB 控制/数据访问；它不是 Windows 本地键盘、鼠标或
磁盘的类驱动，也不是云端认证或画面编解码器。Linux 对应层不应尝试移植 INF 或
`libusbK.sys`，而应使用：

1. `vhci-hcd` 接收远端 USB/IP 设备；
2. Linux 原生 `usbhid` 和 `usb-storage/UAS` 绑定接收后的 HID/存储接口；
3. 如需测试本地复合设备，使用 `configfs + usbip-vudc` 生成虚拟 keyboard/mouse/storage。

## `cdpusbhubhook.sys`：本地 USB Hub hook/观测组件

`cdpusbhubhook/amd64/cdpusbhubhook.sys` 是 PE32+ native kernel driver，静态属性为：

- `SizeOfImage=0xd000`，入口 RVA `0xb000`；
- 直接导入 `ntoskrnl.exe`，未发现它是 USB/IP 数据转发库；
- 字符串包含 `\\DRIVER\\USBHUB`、`\\DRIVER\\USBHUB3`、
  `USB\VID_28EE&PID_8551`；
- 字符串包含 `WmiTraceMessage`、`WmiQueryTraceInformation`、
  `EtwRegisterClassicProvider`、`EtwUnregister`；
- 创建/使用带固定 GUID 的 `\\Device\\CDPUSB-{GUID}` 和
  `\\DosDevices\\CDPUSB-{GUID}` 名称。

这些是 Hub 级拦截、设备识别和 WMI/ETW 观测的静态证据。仅凭字符串不把它解释成
完整的传输协议，但它已经足以说明该组件不属于 Linux USB/IP 数据面，也不符合本项目
“去掉安全和监控相关部分”的目标。Debian 包不携带、不加载、不模拟它。

## 与 ZTE 分支的区别

Windows 包还包含另一套 `chuanyunAddOn-zte` 组件：`libusb0/libusbK` INF、
`ZTEUsbIpSvc.exe`、`usbMsgLib.dll`、`usbtrace.dll`、`qoelog.dll`、
`usbRedirectCheck.dll` 和 `UsbhubHook.sys`。这套分支同时带有 `process_svc_guard`、
QoE、trace 和安全库，不能与基础 CDP `libusbK` 绑定混为一谈。

本项目只复用已经确认的标准 USB/IP 控制面、Linux USB 类驱动和离线 SPICE parser；
ZTE 分支的私有 Windows 服务、Hub hook、trace/QoE、安全策略和闭源传输库均排除在
Debian 生产包之外。

## 对 Linux 适配的结论

Windows 驱动层的真正作用可以拆成两块：

```text
本地物理 USB
  └─ Windows Hub hook / libusbK 设备访问
       └─ 官方用户态 RemoteHub / vendor USB-IP / JWAE 会话
            └─ 云端 USB/IP URB 数据面

Linux
  └─ sysfs/libusb 枚举 + usbip-host（显式本地导出）
       └─ 用户态 USB/IP 控制与 URB 转发
            └─ vhci-hcd -> usbhid / usb-storage
```

因此 Linux 不需要 Windows `.sys` 的兼容层；需要补齐的仍是合法云端会话已经建立后，
把 vendor `g_link` 接到标准 USB/IP control/URB 数据面的适配。该会话前置的
JWAE/SCG 认证和云端地址不应由 Linux 驱动猜测或绕过。
