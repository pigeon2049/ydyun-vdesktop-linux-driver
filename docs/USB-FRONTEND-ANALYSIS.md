# 官方 UOS USB 前端拆分（Step 41）

## 结论

官方 UOS 客户端中的 `chuanyun-redirect` 是 root 权限运行的本地 USB 策略前端，不是
云端 USB 协议本身。它负责接收桌面端操作、检查设备策略、调用本地 RemoteHub/libusb
服务、按 busid 映射设备，并把状态回报给上层。

云端认证、JWAE/SCG 隧道和 USB/IP 主链路仍在另一条闭源会话路径中；本地前端的控制
队列不能替代云端 `g_link`，也不能把 3246 监听端口解释为新版官方拨号接口。

## 静态还原的本地控制队列

`chuanyun-redirect` 使用两条 SysV message queue。静态代码中可见 key `0x3b6`，请求和
状态使用的消息类型分别为 100/101。消息首个 `long` 是命令号，当前可确认：

| 命令 | 官方动作 | Debian 精简适配 |
|---:|---|---|
| 1 | 启动本地 USB 服务，安装设备状态回调 | 不做常驻服务；由显式 `ydyun-usbctl export` 控制 |
| 2 | 停止服务、解除全部设备映射、停止 SCG | 不复制策略服务 |
| 3 | 按 busid 调用 `cdp_usbip_drive_map_by_busid` | 用标准 USB/IP export/attach |
| 4 | 按 busid 调用解除映射 | 用标准 USB/IP unexport/detach |
| 9 | 获取设备列表并返回 JSON | 用 sysfs/usbip 本地枚举 |

设备状态回调还会：

- 检查 deny 列表；
- 写入 VID/PID udev 规则并执行规则刷新；
- 上报带 `deny` 字段的 JSON，供桌面端显示和策略处理。

这些是安全策略、设备管控、udev 注入和 UI/监控状态，不是 USB storage/HID 的数据面。
因此 Debian 包不创建这些队列，不安装同名 root 常驻服务，不复制 deny-list/udev 注入，
也不引入官方日志、质量监控或安全代理。

## `libchuanyun_usbip.so` 的公开边界

官方库可见的关键导出为：

```text
cdp_usbip_start_jwae_service(auth_type, mode, server_ip, server_port, path, log_path)
cdp_usbip_stop_jwae_service()
cdp_usbip_jwae_set_user_passwd(user, password)
cdp_usbip_jwae_set_token(token)
cdp_usbip_start_service(json, void (*on_init)(int))
cdp_usbip_stop_service()
cdp_usbip_drive_map(const char *busid)
cdp_usbip_drive_unmap(const char *busid)
cdp_usbip_is_dev_deny(handle)
```

`cdp_usbip_start_service` 只是转到本机 `android_usbipd_start`/RemoteHub/libusb 导出
服务；`cdp_usbip_start_jwae_service` 才是 JWAE 封装，但仍需要合法 token、会话参数和
厂商库。反汇编显示 drive-map/unmap 直接接收 busid 字符串并转发到 RemoteHub，不能从它推导出
`g_link` 的认证、TLS/KCP、压缩开关或断线时序。

### 动态依赖边界

官方 UOS ELF 的 `DT_NEEDED` 进一步确认：

```text
libchuanyun_usbip.so -> libusbipd.so, libudev.so.1, libjwae.so, libc.so.6
libusbipd.so          -> libjwae.so, libpthread.so.0, libc.so.6
```

其中 `libjwae.so` 是 USB 云端隧道和 `g_link` 的运行时依赖；官方 ELF 的 RUNPATH
还是构建机路径，不能直接作为 Debian 安装路径。因而本项目的 `ydyun-chuanyun-usb`
只在操作者显式提供完整、已授权的厂商库目录时才可尝试加载；默认安装不携带、不开启
这些库，也不通过伪造 `libjwae` 或改变 RUNPATH 来绕过认证。fake ABI fixture 只验证
本项目 loader 的调用约定和内存边界，不验证厂商隧道、认证、TLS 或真实云端 USB。

## 新增的最小 ABI 入口

对 `libchuanyun_usbip.so` 的导出和反汇编继续核对后，确认还有一组比 root 前端更直接
的函数：

```c
int cdpusblib_get_device_list(void **list, int *count);
void cdpusblib_free_device_list(void *list);
int cdpusblib_attach_device(const char *busid);
int cdpusblib_unattach_device(const char *busid);
int cdp_usbip_drive_map(const char *busid);
int cdp_usbip_drive_unmap(const char *busid);
```

设备列表记录大小为 `0x494` 字节；官方填充的稳定字段是 PID `+0x00`、VID `+0x02`、
名称 `+0x04`、busid `+0x44` 和设备类型 `+0x84`。`+0x88` 是 deny/状态策略字段，
精简 loader 不读取、不输出。

据此新增 `ydyun-chuanyun-usb`。它只在显式 `run` 时加载操作者提供的库，接受 `list`、
`attach BUSID`、`detach BUSID`、`map BUSID`、`unmap BUSID`、`stop` 六种命令；其中
`map/unmap` 只对明确指定的存储 busid 调用公开 drive-map ABI，可选地调用已授权的
token/JWAE 入口。
它不创建官方 SysV 队列、不执行 udev 注入、不启用 deny-list、不上报网络质量，也不
作为 systemd 服务启动。仓库中的 fake ABI fixture 已覆盖动态加载、设备列表解析和
attach/detach 调用回归；这证明的是 ABI 边界，不是云端互通。

## Qt viewer 对 USB 的调用边界

官方 `chuanyun-view` 的未剥离符号和 `QSpiceConn::StartConnection()` 反汇编显示：

1. 连接对象在启动阶段先按配置条件调用 `InitCdpUSB()`，再把 `ConnectionConfig` 序列化
   给 `connection_connect_set_config()`，随后设置 session ID 并启动连接线程；USB 初始化
   与画面连接共享生命周期，但不是同一个字节协议。
2. `InitCdpUSB()` 会把 `CommonConfig`、外围设备配置和若干会话字段交给内部
   `DeviceRedirectManager::InitCdpUSB(string,string,int,string)`；`DealCdpUSB()` 再调用
   `StartCdpUSB()`，`UninitCdpUSB()` 调用对应清理入口。
3. 这组 viewer 内部 manager 参数不能由 `cdpusblib_*` 四个公开函数单独推导；当前
   `ydyun-chuanyun-usb` 因此只实现已确认的列表/map/unmap/显式 service ABI，不伪造
   `DeviceRedirectManager` 的私有结构，也不宣称已经替代官方 viewer 的自动 USB 策略。

这解释了当前适配的最后一个真实云端缺口：拿到合法会话后还需记录 `InitCdpUSB` 的四个
参数对应的授权配置和 `StartCdpUSB` 的实际调用结果，才能把最小 loader 扩展为完整的
云端 USB 会话服务。

### 私有 manager 的进一步静态结论

官方 ELF 的 DWARF/符号还给出了以下私有方法签名（仅用于确认边界，不是可链接 ABI）：

```text
DeviceRedirectManager::InitCdpUSB(std::string, std::string, int, std::string)
DeviceRedirectManager::StartCdpUSB(std::string, std::string, int, int, int, std::string)
DeviceRedirectManager::UninitCdpUSB()
DeviceRedirectManager::StopCdpUSB()
```

对 `StartCdpUSB` 的函数体反汇编显示，它至少会：

1. 对一个字符串参数执行 `std::stoi`；
2. 结合 manager 内部的设备容器和会话字段调用 `generateTargetJson(...)`；
3. 将生成的 JSON 通过内部 `MessageQueue::send(...)` 投递，而不是直接调用公开
   `cdpusblib_attach_device()`。

`InitCdpUSB` 的前半段会读取配置 INI、调用 `GetAutoRedirectState`，解析
`parseAllowDevices`/`parseDenyDevices`，随后创建 `MessageQueueWorker` 并在 Qt 线程中处理
内部消息。`UsbDeviceInfo` 是 viewer 自己的 420 字节结构，末尾包含
`bNeedRedirect`、`emDeviceStatus` 和 `emLastOpState`；它与 `libchuanyun_usbip.so`
公开列表中的 0x494 字节记录不是同一个 ABI。

因此这里不能安全地通过 dlsym、对象偏移或手写 C++ 壳调用私有 manager：既缺少稳定的
导出，也会把设备 allow/deny、自动重定向和内部消息策略重新带入精简适配。Debian 实现
继续只使用公开厂商 C ABI（显式外部库）或标准 USB/IP；viewer 私有 manager 作为分析
证据保留，不进入生产包。

### viewer 与公开 `cdpusblib_*` 的运行时路径不同

进一步检查 `chuanyun-view` 的未剥离动态符号后，确认 viewer 没有直接导入
`cdpusblib_get_device_list`、`cdpusblib_attach_device` 或其它 `libchuanyun_usbip.so`
公开符号，也没有把 `libchuanyun_usbip.so` 列为动态依赖。它改为从定制
`libspice-client-glib-2.0.so.8` 导入 SPICE USB manager 的设备枚举、过滤、异步连接和
断开接口，并直接使用 SysV `msgget/msgsnd/msgrcv/msgctl`。

同时，viewer 链接 `libusb-1.0.so.0`，字符串中有 `scanUsbDevicesWithLibusb`；这说明
libusb 负责本地枚举，内部 `DeviceRedirectManager/MessageQueue` 负责本地策略和状态
协调，云端数据面仍在 vendor SPICE/JWAE 连接中。这个组合不能被简化成一个公开
`attach(busid)` 调用，也不能把 Qt manager 私有对象偏移硬接到 Linux 服务。

本项目因此保留两种清晰模式：无厂商库时使用标准 USB/IP；用户明确提供完整、已授权
的厂商公开 C ABI 时才使用 `ydyun-chuanyun-usb` loader。viewer 的策略、监控、自动重定向
和 SysV 队列路径不进入默认 Debian 生产包。

### 官方 USB/IP import reply 的精确长度

对同一套 UOS 客户端内的 `libusbipd.so` 做 DWARF 复核后，`usbip_op_import_reply` 的
body 为 312 字节 (`0x138`)，其前面的 `usbip_op_common` 为 8 字节，因此标准
`OP_REP_IMPORT (0x0003)` 控制帧总长为 320 字节 (`0x140`)。`usbip_usb_device` 的
busid 位于 body 偏移 256，busnum/devnum 位于 288/292；导入请求的 busid body 为
32 字节。这个结果解释了官方反汇编中“发送 `0x0003 + 0x140`”的表述：`0x140` 是
完整回复长度，不是 312 字节之后还要追加的长度。

这项确认只收敛标准 USB/IP 控制面格式，不等于已经获得云端 `g_link`。JWAE/SCG、
ZIME/vendor-SPICE 和真实 URB forwarding 仍必须由合法会话提供；默认 Debian 包继续
不载入官方安全、监控、QoE、策略队列和闭源传输库。

## 适配决策

当前实现保留标准 Linux `usbip-core/vhci-hcd`、`usb-storage`、`usbhid`，并提供显式
USB/IP export/attach 控制和公开 `chuanyun_api.h` ABI 验证入口。真实云端验证仍需要
合法会话样本，重点核对 `g_link` 上的 `0x8008/0x0008` 握手、DEVLIST、PRE_IMPORT、
标准 IMPORT、URB 数据和可选 LZ4 外层封装；本地 SysV 队列分析不能替代这一步。
