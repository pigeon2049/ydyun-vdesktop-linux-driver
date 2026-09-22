# 中国移动云电脑客户端反向验证记录

更新时间：2026-09-22（Asia/Shanghai）

## 1. 来源与方法

本轮从中国移动云电脑官方桌面下载入口取得 Windows、UOS AMD64、麒麟 AMD64 客户端，保存于 `doc/client-packages/`。历史样本只做下载、哈希、解包、脚本/配置/ELF/PE 静态分析；本轮另对当前 UOS 包做了受控的 Debian 实机登录和云会话验证。没有执行 Windows PE，也没有启用官方的安全/QoE/监控 systemd 服务。

官方入口：<https://soho.komect.com/clientDownload>

官方下载 API：`https://soho.komect.com/cube/h5/user/download/urls/v2/1`

| 包 | 本地文件 | SHA-256 | 静态结论 |
|---|---|---|---|
| Windows | `doc/client-packages/mobile-cloud-pc-windows.pkg` | `d2380912e5ddae5bfda4dfd0860d37c0a58f94090b5d87be0b1ec341b3665fc9` | PE/NSIS/Electron，未执行 |
| UOS AMD64 | `doc/client-packages/mobile-cloud-pc-uos-amd64.pkg` | `65fa5a093d73bfef407304b32ed45a1c761d4b105ebdcfa2ffea67e9de751202` | Debian 包，`cmcc-jtydn` 2.23.1 |
| 麒麟 AMD64 | `doc/client-packages/mobile-cloud-pc-kylin-amd64.pkg` | `2ca095dfb60f57e55b0e19e0a15254edfe44c08ac8adf2b5731e0191e4452c45` | Debian 包，结构与 UOS 基本相同 |

## 2. 官方 Linux 包结构

主要安装目录是 `/opt/chuanyun-vdi-client`：

```text
Electron cmcc-jtydn
  ├─ Web/登录/桌面列表/配置/更新/日志管理
  ├─ chuanyunAddOn       -> base 云桌面 SDK、USB/IP SDK、SPICE viewer
  └─ chuanyunAddOn-zte   -> 中兴路径、USB redirect、SPICE viewer、QoE/监控
```

Electron 主进程把云桌面连接参数交给原生插件。非 `zte-*` 桌面调用 `chuanyunAddOn/jsCysdk.js`，再进入 `chuanyunsdk.node` 异步原生 worker；`zte-*` 桌面启动 `bootCypc`，通过 `/tmp/my.sock` 传递 JSON `connect`、`reconnect`、`disconnect`、`exit` 命令。页面脚本没有实现 USB 数据传输、画面解码或底层键鼠协议。

base SDK 的 `chuanyun_api.h` 暴露 `chuanyun_init`、`connectVm`、`disconnect`、连接状态回调、首帧上屏回调和网络质量回调，直接证明“首帧上屏”由原生 SDK 处理。

## 3. USB 存储、鼠标、键盘

### 3.1 base USB/IP 路径

`chuanyun-redirect` 是 Linux x86_64 ELF，依赖 `libchuanyun_usbip.so`、`libusbipd.so`、`libyuv.so`、`libhyffmpegencoder.so`。其符号包括：

- `cdp_usbip_start_service` / `cdp_usbip_stop_service`；
- `cdp_usbip_drive_map` / `cdp_usbip_drive_unmap`，以及按 busid 映射；
- VID/PID/busid/名称枚举、热插拔和设备状态回调；
- `usbip_net_send_usbip_header`、`handle_usbip_req_devicelist`、`handle_usbip_pre_req_import`；
- libusb 异步事件、udev 监听和 LZ4 相关符号。

因此官方 Linux 实现也是“用户态 USB/IP 服务 + libusb/udev + Linux USB 栈”，不需要移植 Windows `.sys`。服务文件以 root 启动 redirect，并安装 udev 访问规则。

### 3.2 ZTE USB 路径

ZTE 的 `usbredirect` 是更重的原生服务，包含 `usbip_send_op_common`、`usbip_recv_op_common`、`usbip_xmit`、`terminal_usbipsvr`、`importDev` 和 `usbip_status` 等证据；它接收来自 Spice 的设备导入命令，再建立 USB/IP 转发。

`usbip.conf` 还包含 USB 类、VID/PID、存储模式和读写策略。关键项为 `USB_REDIRECT_ALLOW=1`、`DISK_RW_ALLOW=1`；音频、网卡、虚拟/QEMU、Hub 等类别默认被过滤。`DISK_RECIRECT_ALLOW=0` 是厂商的“磁盘专用模式”开关，不能简单解释为所有 USB 存储均禁止。

### 3.3 与本项目的对照

```text
云端 USB 控制/数据面
  -> 用户态 USB/IP 控制器
  -> usbip-core + vhci-hcd
  -> usb-storage / usbhid / evdev
```

本项目已经在隔离虚拟设备上完成 USB 存储、boot keyboard、boot mouse 的 `usbipd -> ydyun-usbctl -> vhci-hcd` 端到端验证。最终 Debian 包不携带官方闭源 `usbredirect`、`libzxsecurity`、QoE/trace 库，也不安装 SUID 监控守护进程。

## 4. 画面和键鼠控制链路

官方 Linux 客户端的画面链路为：

```text
云桌面/接入网关
  -> 原生 VDI/Spice 通道
  -> libspice-client-glib-2.0 或 libspice-client-glib-zte-2.0
  -> display surface/stream + 视频/无损区域
  -> chuanyun-view 或 uSmartView_VDI_Client
  -> Qt/X11/Wayland 窗口
```

证据：

- `chuanyun-view` 依赖 `libspice-client-glib-2.0.so.8`，并引用 `spice_main_channel_update_display`、`spice_main_channel_send_monitor_config`、`spice_inputs_channel_key_press`、`spice_inputs_channel_button_press`、`spice_inputs_channel_position`；
- `uSmartView_VDI_Client` 依赖厂商版 `libspice-client-glib-zte-2.0.so.8`，并引用 `spice_gtk_connect_session`、`spice_gtk_get_display_frame_param`、`spice_gtk_get_display_rgb_data`、`spice_gtk_key_down/up`、`spice_gtk_mouse_motion`、`spice_gtk_mouse_button_down/up`；
- 包含 FFmpeg、OpenH264、dav1d、swscale 等解码依赖，配置中有硬件刷新、视频帧队列、缓存和帧率控制；
- `chuanyun_api.h` 的 `FrameInfoCb` 是首帧上屏回调。

这和原 Windows ICE 静态分析一致：整屏不是 USB/IP，也不是 `uSmartPlayer` 的普通媒体流，而是带厂商扩展的 SPICE/VDI display channel，包含 surface、stream、编码帧、缓存/丢帧策略及输入/光标通道。本项目已完成标准 SPICE framing 和 display stream 元数据离线解析，但真实云端仍需要认证、TLS/KCP/厂商扩展样本才能实现完整 Linux viewer。

键盘鼠标有两条不同路径：控制云桌面窗口时，键盘鼠标经 Spice input channel；把本地 USB 键盘鼠标作为 USB 设备交给云端时，经 USB/IP/HID redirect。两条链路必须保持分离。

## 5. 安全、监控、遥测边界

官方包中确认存在以下非核心组件：

- `QoEAgent`、`qoe.service`、`zqoe.service`；
- `monitor_qoe.sh`、`monitor_sohosdk.sh`，每 5 秒检查并重启 QoE、USB redirect 等进程；
- `collect_top_ten_data.sh`，收集 CPU/内存、进程、打开文件和挂载文件系统；
- `process_svc_guard` 配置及安装/卸载脚本中的进程终止逻辑；
- `libqoelog`、`libusbtrace`、`libzxsecurity`、`libSafeCRT` 等日志、追踪和安全依赖；
- Electron `probe.js` 的 Windows 分支收集 CPU、内存、磁盘、网卡、MAC、显示器、USB VID/PID、性能和流协议信息并上传；Linux 分支虽导出空 probe，但原生 QoE 组件仍随包提供；
- 设备信息、序列号、日志归档、日志上报和在线更新逻辑。

本项目最终 Debian 适配明确不复制这些部分：不执行官方维护脚本、不启用 QoE/监控 service、不加入进程守护、上传、日志采集，不安装 Windows 安全或网络拦截组件；只保留 USB/IP 控制器、协议边界检查、显式配置和最小生命周期管理。

## 6. 结论与未完成项

已经验证：

1. Debian trixie x86_64 的标准 USB/IP 控制器已完成并打包；
2. USB 存储、键盘、鼠标已经完成隔离端到端验证；
3. 官方 Linux 客户端采用用户态 USB/IP/libusb/udev 与原生 Spice/VDI SDK；
4. 安全、监控、QoE、遥测、网络拦截和 Windows 内核组件已从当前 Debian 包排除。

仍不能声称完成：

1. 尚无真实中国移动云桌面登录会话、令牌和云端抓包，不能宣称生产云端 USB/画面端到端互通；
2. ZTE 3246 控制面完整时序、压缩协商和多设备配对仍需真实样本；
3. ICE/Spice 的 TLS/KCP/多路径和厂商 display 扩展仍需真实样本；`ydyun-ice-probe` 目前是离线探针，不是完整 viewer；
4. 官方闭源原生库只作为行为和协议边界参考，不直接重新打包进我们的 Debian 驱动。

下一阶段顺序：标准 USB/IP 云端互通 -> 3246/ZTE 控制面验证 -> USB 热插拔/重连 -> 独立 `ydyun-ice` 接收器 -> display stream 解码、窗口呈现和 Spice 输入/光标支持。

## 7. SDK 符号级交叉验证

为了避免把“USB 设备转发”和“画面转发”误合并，继续对官方包做了符号级静态核对：

- `libchuanyun.so` 导出 `connectVm`；`libchuanyun_usbip.so` 导出
  `cdp_usbip_start_jwae_service`、`cdp_usbip_jwae_set_token` 和
  `cdp_usbip_jwae_set_user_passwd`。这些接口属于厂商会话编排，不能用标准 USB/IP
  `DEVLIST`/`IMPORT` 代替；
- 官方 `libspice-client-glib` 导出 connection/session 建立函数、display stream
  处理函数以及键盘按键、鼠标按钮/坐标输入函数；`chuanyun-view` 还能看到
  `qspice-display-channel.cpp` 和 `qspice-inputs-channel.cpp` 的本地调试符号；
- Windows 包的 `RemoteDesktopSDK.h` 将 `startUsbRedirect`/`stopUsbRedirect`、
  `getDesktopStreamParam`/`getUpScreenStreamParam` 和 `FrameInfoCb` 分开定义。

这组证据确认最终 Linux 客户端应由三个独立层组成：USB/IP 设备转发、Spice 输入、
Spice/厂商 display stream。当前包只交付第一层以及离线协议探针；真实画面 viewer 还
需要合法云会话中的认证参数、TLS/KCP 配置和帧样本，不能仅靠下载包静态推导完成。

## 8. 从登录 API 到画面的完整静态调用链

继续检查官方 Linux 客户端的未压缩 Electron 源码和 `chuanyun-view` 调试符号，得到更
具体的调用顺序：

```text
HTTPS /terminal/cc/getFirmAuth/v1
  -> d.data: vmId、vmUserName/vmPassword、vmcIp/vmcPort、cagIp/cagPort、spuCode 等
  -> Electron worker-connect
  -> zteWorker.connect(...) 或 base runWorker.connect(options)
  -> libjwae / SCG 隧道
  -> chuanyun-view: connection_connect_set_config + set_session_id
  -> vendor SPICE display/input channels
```

可复核的静态证据：

- `home.vue` 对普通云桌面请求 `/cc/getFirmAuth/v1`，成功后把整个 `d.data` 交给
  `mainApi.connectWorker`；主进程对 `zte-*` 桌面把 `vmUserName`、`vmPassword`、
  `vmId`、`vmcIp`、`vmcPort`、`cagIp`、`cagPort` 以及可选 SCG 地址/端口传给
  `zteWorker.connect`；
- 请求层把业务 body 先包装为 `{data: base64}`，使用内置公钥做 RSA 无填充分块加密，
  每块最多 117 字节，并用 HMAC-SHA256 生成 `X-SOHO-Signature`；这解释了为什么
  仅探测 3240/3246 无法凭空建立生产云会话；
- `chuanyun-view` 的 `QSpiceConn::StartConnection` 在调用
  `connection_connect_client` 前，把 `ConnectionConfig::toJson()` 交给
  `connection_connect_set_config`，再把连接信息中的 session ID 交给
  `connection_connect_set_session_id`；同一流程还调用 `jwae_set_token` 和
  `jwae_start`；
- `ConnectionConfig` 的序列化字段名可从未剥离调试符号确认，包括 `commonConfig`、
  `peripheralConfig`、`encodingModeThresholdConfig`、`videoEncodingParamConfig` 和
  `networkAdaptationConfig`；`CommonConfig` 还包含 `usbPortRedirectEnabled`、
  `networkTransportProtocol` 等策略字段。

这证明画面接收器需要厂商 SCG/JWAE 会话上下文，而不是只实现一个裸 SPICE TCP 客户端。
因此本项目继续把 `ydyun-usbctl` 与未来的 `ydyun-ice` 分开：前者负责已验证的标准
USB/IP 数据面，后者应在取得合法 `getFirmAuth` 响应和真实会话样本后接入 SCG/SPICE；
不复制官方安全、QoE、监控和日志上报模块。

## 9. Linux 画面接收的可验证边界

官方 UOS/麒麟客户端随附 `libspice-client-glib-zte`、FFmpeg/OpenH264/dav1d 等原生
依赖，说明最终画面接收器至少包含 SPICE display channel、编码帧解码和窗口合成三层。
但官方 `chuanyun-view` 还依赖 `libjwae`/SCG、厂商连接配置和 session ID；仅安装 Debian
公开的标准 SPICE 客户端不能替代这段会话建立过程。

当前 Debian 包不复制这些闭源库，也不执行官方 ELF。包内的 `ydyun-ice-probe` 只在用户
提供已经合法解出的标准 SPICE 字节时，校验 display framing 并提取有长度边界的编码样本；
后续可用系统 FFmpeg 做离线解码验证。这样既保留了画面适配的可测试路径，也避免把官方
QoE、进程守护、日志上报、安全库和未经验证的私有握手带入驱动包。

## 10. ZTE 连接参数的 IPv6 分支

主进程的 `zteWorker.connect` 静态调用还显示了可选 `cagIpv6`、`scgIp`、`scgTcpPort`
和 `scgUdpPort` 参数。也就是说，Linux 适配不能只假定一个 IPv4 CAG；会话描述层必须
保留 IPv6 CAG 和 SCG 的独立地址/端口，后续连接层再根据真实会话选择路径。当前
`ydyun-session-probe` 已覆盖这一字段，但仍不会伪造或主动请求云端会话。

## 11. `libjwae` ABI 的可复用边界

官方 `libjwae.so` 的动态符号可以确认 `jwae_set_token`、`jwae_set_user_passwd`、
`jwae_set_fd_callback`、`jwae_start`、`jwae_stop`、`set_cloud_config` 和
`jwae_get_connection_info_new` 等入口，但包内没有对应 C 头文件，也没有可供复用的
调试类型信息。仅凭 ELF 符号名不能可靠确定参数布局、回调 ABI、内存所有权和线程模型。

因此 Linux 适配不直接用猜测的 `ctypes`/C ABI 调用 `libjwae`，也不把官方库复制进
生产包；后续只有在获得合法 SDK 接口或真实会话样本后，才实现明确的 JWAE/SCG 适配层。

## 13. 配置字段级的监控剥离边界

对 `chuanyun-view` 的 DWARF 类型信息进一步核对后，`CommonConfig` 同时包含两类
字段：`usbPortRedirectEnabled` 和 `networkTransportProtocol` 属于连接/转发核心；
`sdkPointCollectPeriod`、`sdkPointReportPeriod`、`sdkPointEnbaled`、
`publicProbeTimeout`、`publicProbeDomain` 则属于质量采集或公网探测。另有摄像头、磁盘
映射、剪贴板和文件拖放字段，属于当前目标之外的外围功能。

因此 Debian 适配保留 USB/IP、HID、存储和必要的认证/传输安全边界，排除 SDK 采集、
公网探测、摄像头、磁盘映射、剪贴板和文件拖放实现。完整静态字段表位于
`doc/CLIENT-ABI.md`。

## 12. 官方 SPICE connection ABI 的 DWARF 证据

与 `libjwae` 不同，官方麒麟包内的 `libspice-client-glib-2.0.so.8.8.2` 带有 DWARF
调试信息。静态解析确认了以下调用边界：

```c
SpiceConnection *connection_new(void);
int connection_connect_set_config(SpiceConnection *, const char *json);
int connection_connect_set_session_id(SpiceConnection *, const char *session_id);
int connection_connect_client(SpiceConnection *);
int connection_connect_session(SpiceConnection *);
void connection_destory(SpiceConnection *);
```

`_SpiceConnection` 的大小为 104 字节，并包含 JSON/session 字符串指针和内部
`SpiceSession`/`GMainLoop`/channel 对象。这个结果把“认证响应 -> 连接配置 -> session ID
-> vendor SPICE display/input”链路从符号级证据推进到了静态类型级证据；完整字段表在
`doc/CLIENT-ABI.md`。

这仍不是可直接运行的 Linux 客户端：JSON 的完整版本语义、JWAE/SCG 前置隧道、TLS/KCP
扩展和云端真实帧样本仍缺失。因此只用于设计接口边界，不执行官方闭源库，不把 ABI
猜测代码放入 `ydyun-usbctl`。

## 14. 官方 USB/IP 压缩分支

对官方 Linux `libusbipd.so` 的 `network_send_data_with_compressed`、
`try_write_compress_LZ4` 和接收路径静态分析显示，USB 数据可被包在 9 字节网络头中：
类型、原始长度、载荷长度均明确存在，type 1 使用 raw LZ4 block，type 0 保持原文。
这解释了官方库中同时出现标准 USB/IP 处理函数和 LZ4 网络函数的原因：压缩属于
数据链路封装，不是新的 USB 类协议。

Linux 适配已把它限制为反向桥的显式 `compression=off|lz4` 选项。默认关闭；开启后
只在标准 USB/IP 数据与该封装之间转换，仍不执行官方 `libusbipd.so`，不调用未知 ABI，
也不自动探测云端模式。没有真实会话样本时，压缩分支仍标记为待验证。

`network_send_data()`/`network_recv_data()` 的调用图还确认了该封装覆盖
`send_xfer_data` 及 USB/IP DEVLIST/IMPORT 路径；连接对象的压缩开关位于静态对象偏移
`+0xe30`。这使当前实现的边界更明确：只转换外层 stream，不修改 USB/IP 标准头、URB
和设备描述符，也不触碰官方 JWAE/SCG 认证层。

官方 viewer 的画面回调包括 surface 创建/失效/销毁、OpenGL invalidate、脏区标记和
光标更新；输入回调包括鼠标位置、按钮、键盘按键和锁定键状态。由此反向验证：整屏
画面不应通过 USB/IP 模拟，鼠标键盘也有两种不同语义——USB 转发是把物理 HID 设备
交给远端，桌面控制则是把本地 GUI 事件发到 SPICE inputs channel；两者在 Linux 适配
中应保持独立开关和独立测试。

## 15. 官方 USB 主链路与本地 RemoteHub 的区分

`libusbipd.so` 的静态调用图显示，云端 USB 主链路先由 JWAE/SCG 提供 `g_link`，再由
`handle_usbip_link_init` 发送 `0x8008` 主链路请求并等待 `0x0008` 响应。链路建立后，
官方处理 `0x8005` 设备列表和私有 `0x8009` 预导入请求，随后回到标准 `0x0003` import
回复和 libusb forwarding。数据均经过 `network_send_data`/`network_recv_data`，所以
可选 LZ4 是该主链路的外层封装。

`android_usbipd_start(json,on_init)` 则是另一条本机能力：它启动 RemoteHub/libusb
导出服务，管理 hotplug 和本机设备列表；它不是云端连接器。`libchuanyun_usbip.so`
的 `cdp_usbip_start_service` 只是转调该本地服务入口，不能据此得到 JWAE/SCG 的云端
连接 ABI。

这解释了为什么标准 `usbip attach` 和旧式 3246 桥的验证不能替代真实官方会话验证：
前者验证 Linux USB 栈，后者最多验证历史控制面，而官方新客户端的链路入口在闭源
JWAE/SCG 内部。适配代码因此继续不复制官方二进制、不猜测 `g_link` 布局，也不移植
`deny_dev`、监控和热插拔策略；待有合法真实会话样本后，再单独实现主链路协议适配。

## 16. 画面控制面与编码/显示数据面

官方 Linux `libchuanyun.so` 的导出符号包括 `GetDesktopStreamParam()` 和
`GetUpScreenStreamParam()`；只读字符串还出现 `renderFirstFrame`、`SendDesktopStream`、
`SendUpScreenStream`、`SendMonitorIndex`。这把“获取/上报画面流参数”和“实际画面通道”
区分开：前者属于会话控制面，后者仍由 vendor SPICE/ICE display/input channel 承载。

包内同时携带 FFmpeg、OpenH264、x264/x265、dav1d 等依赖，说明官方客户端可能按会话
编码策略选择解码器；仅凭依赖列表不能断言某一次云会话使用了哪种编码。Linux 端应等
真实会话样本确认帧封装、关键帧/脏区和色彩格式后，再接入系统 FFmpeg/硬件解码；
`SendMonitorIndex` 属于采集/策略边界，不纳入精简适配。

## 17. 官方 Linux 头文件与可选会话适配器

官方包随附的 `chuanyun_api.h` 明确导出 `chuanyun_init`、`connectVm`、`disconnect`、
`deInit`，并定义连接状态、首帧、网络状态和桌面事件四类回调。Linux 精简适配器只
连接前两类功能性回调，日志服务和网络状态/事件回调保持为空；它不加载或复制官方库，
只有操作者显式调用 `ydyun-chuanyun-session CONFIG` 时才通过 `dlopen` 绑定外部库。

这使真实会话验证从“猜测闭源对象 ABI”变为“使用官方公开头文件验证调用边界”，但
不会改变事实：认证、JWAE/SCG、vendor SPICE/ICE 和实际画面帧仍由外部官方运行时
提供。适配器是兼容验证入口，不是绕过认证的连接器。

## 18. 从 DWARF 还原 JWAE 到画面/USB 的连接顺序

进一步检查官方 `chuanyun-view` 的 DWARF 与 `QSpiceConn::startSCG()` 反汇编后，还原出：

```text
getFirmAuth JSON
    -> VM/VMC/CAG/SCG 地址和端口
    -> StartConfig{mode, auth_type, TCP/UDP, ping_ip}
    -> jwae_set_token(token)
    -> jwae_start(&StartConfig)
    -> vendor SPICE connection
       -> display surface/stream -> 解码/窗口
       -> inputs mouse/key -> 云桌面
    -> USB 主链路在 JWAE/SCG g_link 上执行 0x8008/0x0008
       -> DEVLIST/PRE_IMPORT/标准 IMPORT -> 本地设备 forwarding
```

`StartConfig` 为 112 字节；官方 viewer 的 DWARF 字段和偏移已写入 `doc/CLIENT-ABI.md`。这项证据解决了“画面是否通过 USB 转发”的疑问：画面和 GUI 输入走 vendor SPICE/ICE channel，USB storage/HID 走同一会话提供的独立 USB/IP forwarding；二者共享 JWAE/SCG 会话，但不共享数据协议。

仍不能据此直接连接云端：`jwae_start` 的认证、DTLS/KCP、加密和内部回调属于闭源实现，`config_path`/`ping_ip` 也可能随版本和会话变化。当前只把静态布局记录为 ABI 证据，没有把 `libjwae.so` 拷入 Debian 包或执行官方闭源代码；真实联调仍需合法会话参数、首帧/USB 数据样本和断线时序。

## 19. 从 Electron 源码交叉验证画面与监控分层

官方 UOS `app.asar/src/main/index.js` 的连接分发逻辑显示：

```text
worker-connect(options)
    -> 非 zte-*：runWorker.connect(options)
    -> zte-*：zteWorker.connect(vmUserName, vmPassword, vmId,
                                vmc/cag/scg 地址和端口)
    -> 原生 SDK 回调
       -> vendor SPICE/ICE display + inputs
```

Electron 主进程只负责窗口、连接参数和回调转发，没有发现画面帧解码或 USB URB
数据处理。`src/main/usb.js` 的设备扫描仅读取 VID/PID、接口类和设备类型，用于上层
外设状态；USB 数据面仍在原生 SDK/USB/IP 库中。

同时，官方 `src/main/probe.js` 是独立的软探针路径：它按远端配置开启性能、推流协议、
外设和显示器信息采集，读取 CPU/内存/磁盘/Wi-Fi 强度、RTT/丢包率，并向
`/stream/protocol/send/v2` 等接口上报。该路径属于监控/QoE，不是画面或 USB 数据面；
精简 Debian 适配器不复制 `probe.js`、不启动采集定时器、不发送这些上报。

因此画面适配的最低必要边界仍是：认证响应 -> JWAE/SCG 会话 -> vendor display/input
通道 -> 解码/窗口；USB storage/HID 则走会话提供的独立 USB/IP forwarding。禁用软探针
不会影响上述两个功能通道。

## 20. 官方 Qt viewer 的二进制边界

官方 UOS 包还包含独立的 `ccsdk/uos/bin/chuanyun-view`。静态 `DT_NEEDED` 显示它直接
依赖 Qt5 GUI/Multimedia/Network、`libjwae.so`、定制
`libspice-client-glib-2.0.so.8`、OpenSSL、FUSE 和 USB 库；它不是 Electron 页面，也
不是 Windows USB 驱动的 Linux 替身。

该 ELF 的未剥离符号给出比通用 SPICE harness 更精确的功能边界：

| 官方符号 | 静态含义 |
|---|---|
| `QSpiceConn::startSCG`、`StartConnection`、`openSession` | JWAE/SCG 与 vendor SPICE 会话建立 |
| `QSpiceWidget::displayPrimaryCreate`、`displayInvalidate`、`displayOpenGlInvalidate` | 显示 surface 创建、脏区更新和 OpenGL 更新 |
| `QSpiceHelper::inputs_modifiers`、`main_mouse_update` | 键盘修饰键和鼠标事件上行 |
| `QSpiceWidget::setClientCursor`、`setMoveCursor` | 云端光标更新 |
| `QSpiceConn::StartCdpUSB`、`InitCdpUSB`、`DealCdpUSB` | USB redirect 控制/数据面的独立入口 |

因此 Linux 画面客户端不能只复制 `vhci-hcd`，也不能把 USB/IP 字节当作画面帧；需要
保留独立的 display/input 接收窗口和 JWAE/SCG 前置。当前生产包只提供标准 SPICE
显示/输入实验 harness 与外部官方 ABI loader，不执行或重新分发该闭源 `chuanyun-view`。

## 21. UOS Electron USB 逻辑的反向验证

进一步逐行核对 UOS `src/main/usb.js` 和打包后的 `out/main/index.js`：Electron 侧只用
Node `usb` 枚举本地设备、读取 VID/PID、设备/接口 class，并每 3 秒比较插拔变化；它把
`plug/unplug/link/unlink` 记录写入本地状态，供 UI 和独立 probe 上报使用。没有看到它
承载 USB/IP 的 URB、远端 socket 或画面帧。

这反向验证了 Linux 精简实现的拆分方式：本地设备识别可由 sysfs/usbip 替代，实际
storage/HID 数据应交给 `usbip-core`、`vhci-hcd` 和 Linux `usb-storage`/`usbhid`；
Electron 的轮询、状态上报和 QoE 采集不属于转发必需路径，继续排除在 Debian 包之外。

## 22. 官方 Linux 客户端的三条数据面与 JWAE 边界

本轮对 UOS `chuanyun-view`、`libspice-client-glib-2.0.so.8.8.2`、`libusbipd.so` 和
`libjwae.so` 做了定向符号/字符串交叉验证，得到更严格的分层：

```text
授权 getFirmAuth/session
        |
        +--> JWAE/SCG：认证、TLS/DTLS、TCP/UDP/KCP、trunk/side-channel
        |       |
        |       +--> vendor SPICE display：surface/stream/脏区/光标 -> 解码窗口
        |       +--> SPICE inputs：桌面键盘/鼠标事件 -> 云端桌面
        |       +--> USB/IP：DEVLIST/PRE_IMPORT/IMPORT -> 远端 USB storage/HID
        |
        +--> Electron/UI：窗口、状态显示和本地设备枚举（非数据面）
```

`chuanyun-view` 明确导入 `spice_inputs_channel_*`、display surface/GL 接口和
`spice_usb_device_manager_*`；其本地 `DeviceRedirectManager`、`MessageQueue`、
`scanUsbDevicesWithLibusb` 属于私有自动重定向控制面。它没有直接导入公开
`cdpusblib_*`，所以不能把公开 USB C ABI loader 误当成官方 viewer 的完整替代品。

`libusbipd.so` 的 `handle_usbip_link_init`、`handle_usbip_req_devicelist`、
`handle_usbip_pre_req_import`、`network_send_data`/`network_recv_data` 和 LZ4 函数
共同证明：USB/IP 请求在 JWAE 提供的主链路字节流上运行，压缩是外层封装；
`android_usbipd_start` 则是本机 RemoteHub/libusb 导出能力。

`libjwae.so` 还包含 side-channel、trunk、redirect-manager、monitor-server、DTLS
和网络质量结构。它应被视为闭源认证/隧道运行时，而不是可移植的 Linux 画面库；
精简适配不复制其监控、QoE、trace、探针或安全策略路径。

## 23. ZTE 定制 SPICE 库的画面证据与排除边界

进一步对官方 `chuanyunAddOn-zte/ccsdk/lib/libspice-client-glib-zte-2.0.so.8.5.0`
做了只读依赖和符号审计。该库直接依赖 FFmpeg/OpenH264/dav1d、`libzx_yuv2rgb.so`、
`libzxsecurity.so`、ZIME data-channel 和 OpenSSL；它暴露的功能同时覆盖：

- `spice_gtk_get_display_frame_param`、`spice_gtk_get_upscreen_display_frame_param`、
  `spice_gtk_get_display_rgb_data`、`spice_gtk_get_display_DMA_data`：显示帧参数、RGB/DMA
  取帧和硬解/零拷贝边界；
- `spice_inputs_*`、`spice_gtk_mouse_*`、`spice_gtk_key_*`：桌面输入上行；
- `avcodec_get_screen_to_client`、ZIME `CreateDataChannel/CreateDataStream`：编码帧解码
  与厂商数据通道；
- `spice_gtk_send_qoeagent_msg`、`spice_gtk_set_network_quality_stat_param`、
  `spice_gtk_set_perfmon_log*`、`spice_gtk_insight_report`、远程协助和安全接口：非核心
  监控/策略/安全路径。

这进一步证明画面是独立的 display/data-channel + 解码链路，而不是 USB/IP；也证明不能
把官方 ZTE `.so` 原样复制到精简包。Linux 适配只复用已经验证的标准 SPICE display/input
边界和系统编解码器，保留 `libzxsecurity`、QoE、perfmon、日志、远程协助等依赖在官方
运行时之外。

## 24. ZIME 数据引擎是厂商传输层，不是可替代的标准 SPICE socket

对随 ZTE 客户端提供的 `libZIMEDataEngine.so` 做了只读导出和字符串分析。其公开 API
和内部符号显示：

- 数据通道/数据流创建、销毁、发送、接收和回调：`ZIME_CreateDataChannel`、
  `ZIME_CreateDataStream`、`ZIME_SendData`、`ZIME_ReceiveData`、callback/event；
- 传输实现包含 `ZIMEQuic`、`ZIMESctp`、external transport、FEC encoder/decoder 和
  stream block state；
- 运行时还包含证书更新/查找、cipher preference、QoS 统计、engine/channel profile、
  alarm property 和 engine statistics。

因此 ZTE 画面路径更准确地表示为：

```text
JWAE/SCG 会话 -> ZIME QUIC/SCTP/外部传输 -> vendor SPICE display/input -> 解码/窗口
```

`ZIME` 的存在解释了官方库中同时出现标准 SPICE message、UDP/TCP 切换、FEC、硬解和
QoE/证书接口；它不是能够仅凭 host/port 复现的标准 SPICE 端口。正式 Debian 包不带
`libZIMEDataEngine.so`，不实现其私有传输、证书、FEC 或监控 ABI；`ydyun-ice-probe`
仍只处理操作者提供的已解码离线样本。

## 25. Electron USB 模块是外围信息上报，不是 USB 转发数据面

对官方 UOS/Kylin `app.asar/src/main/usb.js` 和 `probe.js` 的源码再次核对，得到更明确
的边界：

- `usb.js` 通过 Node `usb` 模块调用 `getDeviceList()`，每 3 秒按 VID/PID 轮询插拔；
  它只计算 `deviceVid`、`devicePid`、接口类字符串和 `HidKeyboard/HidMouse/MassStorage`
  等类型标签，没有打开 USB bulk/interrupt endpoint，也没有实现 USB/IP URB。
- `probe.js` 在 `peripheralSwitch` 开启时启动 `initUsb()`，把事件缓存发送到
  `sc/probe-terminal-portal` 的 `/peripheral/send/v1`；断开时还会发送一次剩余缓存。
- 同一 `linkFunc()` 还按 `performanceSwitch`、`streamProtocolSwitch` 启动性能/流协议
  采集和上报。这些开关、日志和设备枚举属于终端探测/监控控制面，不是键盘、鼠标、
  存储转发的必要数据面。

这与 ELF/PE 证据相互印证：真正的 USB storage/HID forwarding 在
`libusbipd.so`/RemoteHub/JWAE 链路中，真正的画面和桌面输入在 vendor SPICE/ZIME 链路中；
Electron `usb.js` 只负责信息上报。Debian 适配包没有移植这段轮询、外围上报、性能统计
或流协议监控逻辑，因此去掉安全/监控部分不会削弱 USB HID/storage 数据面。

## 26. 官方 `libchuanyun.so` 到 viewer 的反向验证

官方 UOS `libchuanyun.so` 的 `connectVm` 并不负责直接绘制画面。其异步连接线程在
VM ready 后构造并启动：

```text
./chuanyun-view spice://127.0.0.1:10800+<opaque session/config fields>
```

官方 viewer 的 `main` 接收这个位置参数，`QSpiceConn::parseURL` 按 `://`、`+` 拆分，
再把连接地址和会话/config 交给 vendor SPICE/JWAE。viewer 导入的标准数据面符号包括
`spice_main_channel_update_display`、`spice_inputs_channel_*` 和 display surface/stream
处理；同一个进程还包含 USB redirect 的独立入口。因此当前画面链路可以进一步收敛为：

```text
getFirmAuth/授权会话 -> JWAE/SCG -> 本地会话端口/opaque URL
    -> chuanyun-view -> SPICE display/inputs -> 解码窗口与键鼠上行
```

`spice://127.0.0.1:10800` 是该版本官方控制器生成的本地会话端点，不等于可以脱离
认证直接访问的云端公开 SPICE 端口。后缀字段没有足够公开 ABI 语义，精简实现只验证其
边界并保持不打印；不会猜测 token、拼接 shell 命令或加载 `libjwae`/安全监控库。

这项证据也解释了当前 Debian 适配的交付边界：标准 SPICE framing、display stream 元数据、
输入编码/解码和 GTK harness 已可验证；真实画面还需要授权会话产生的首帧/连接样本，
真实远端 USB 还需要 JWAE/SCG 提供的 USB/IP forwarding 样本。官方 viewer 的监控、QoE、
探针、远程协助和安全策略不属于 Linux 数据面，继续排除。

### 26.1 `ConnectInfo` 字段与 URL 拼接顺序

进一步从 `libchuanyun.so` 的 `ConnectInfo::serialize` 还原出字段名：`traceId`、
`scAuthCode`、`hostIp`、`scgIp`、`scgIpv6`、`scgTcpPort` 和 `scgUdpPort`，其中前七个
对象成员的偏移分别为 `0x20/0x40/0x60/0x80/0xa0/0xc0/0xe0`，均为 `std::string`。
连接线程实际把 `scgTcpPort`、`scgUdpPort`、`scAuthCode`、`traceId` 与 operator 内部
地址、两个 `InitNamedPipe` 相关的线程字符串用 `+` 串到 viewer URL 后缀中。

这项结果让 URL 边界比单纯 strings 更可靠，但仍不足以构成独立登录协议：字段中有
认证码、内部地址和随机/管道值，且官方 viewer 后续还要通过 JWAE/SCG 和 vendor SPICE
配置。Linux 适配因此只固定安全的 `scheme://host:port` 边界和长度校验，不猜字段
语义、不打印后缀、不复制官方的 shell 启动和监控/安全逻辑。

### 26.2 Electron 原生 addon 的输入边界

官方 `chuanyunAddOn/ccsdk/src/simpleasyncworker.h` 还公开了 Node addon 到 SDK 的
中间结构 `vmInfo`：`strSeverIP/nSeverPort`、日志服务器、终端 SN、unit type、VM ID、
用户名、auth code、biz code，以及 node IP/port。Electron 的 `jsCysdk.js` 只把
`init`、`connect`、`restart`、`disconnect` 等命令和这一组参数交给 native addon；
它没有实现显示帧、USB URB 或输入报告。

因此完整的官方链路是：

```text
Electron vmInfo/options
  -> chuanyunsdk.node / libchuanyun.so public API
  -> vendor JWAE/SCG + 本地 viewer URL
  -> chuanyun-view parseURL
  -> SPICE display/input 与独立 USB redirect
```

这进一步支持 Debian 侧只保留 public session ABI、标准 SPICE 和 Linux USB/IP 的拆分；
Electron 的日志、外围轮询、monitor index、性能/QoE 上报不进入适配包。

viewer 的反汇编还确认 `parseURL` 对 URL 后缀索引 1、2、5、6、7、8 做字符串写入，
对索引 3、4 做十进制整数解析，索引 0 保留为 Qt 字符串；这说明后缀是内部连接配置
载体，而不是可以脱离授权会话独立生成的 URI。生产 parser 只输出 endpoint、字段数和
脱敏摘要。

## 26.3 官方 `chuanyun-redirect` 根服务与清洁适配边界

对 UOS 包中的 `chuanyun-redirect.service`、启动脚本和 ELF 字符串再次交叉核对后，确认
它是一个以 root 运行、`Restart=on-failure` 的策略前端，而不是不可替代的 Linux 内核
驱动。服务设置专用 `LD_LIBRARY_PATH`，加载 `libchuanyun_usbip.so`/`libusbipd.so`，
并通过以下路径管理本机状态：

- `libudev` 监听和写入 `/etc/udev/rules.d/99-usb-permissions.rules`；
- SysV `msgqueue`、cJSON 状态表和 `send_usb_state_to_main_cjson` 一类的主程序回报；
- deny/allow、设备状态、VID/PID 和热插拔回调；
- `cdp_usbip_start_service`、`cdp_usbip_drive_map/unmap` 等公开 USB 包装入口。

这些调用负责“哪些设备可见、如何上报、何时重启和如何给主程序显示状态”，属于安全、
监控和策略控制面。真正的 USB/IP 数据面仍是设备列表/预导入/导入之后的 USB/IP 报文和
Linux `usbip-core`/`vhci-hcd`/类驱动。移除这个 root 前端不会要求移植 Windows 安全驱动，
也不会移除 HID/storage 的协议承载能力，但会刻意不复制厂商的默认设备策略。

清洁 Debian 包的对应设计是：用户显式指定 busid，使用标准 USB/IP 和内核 VHCI，服务
默认 disabled/inactive，不改 udev 规则、不建 SysV 策略队列、不读取 deny-list、不上报
设备状态；官方 C ABI loader 只在操作者显式提供库和授权配置时运行，且回调置空。这样
既保留存储/键鼠 forwarding 的可审计数据面，又把安全/监控/遥测从默认安装面剥离。

## 27. `usbredirect` 的本地导出证据与清洁实现边界

对 UOS/Kylin 包中的 `chuanyunAddOn-zte/ccsdk/bin/usbredirect` 做了只读 ELF 依赖、字符串
和配置交叉核对。它不是单纯的设备列表程序，而是一个带厂商控制面的 USB/IP server：

- ELF 直接依赖 `libzxsecurity.so`、`libqoelog.so`、`libusbtrace.so`、`libusbMsgSR.so.1`
  和 `libusb-1.0.so.0`，同时包含 `usbip_send_op_common`、`usbip_recv_op_common`、
  `usbip_xmit`、`usbip_stream`、`stub_rx_loop`、`stub_tx_loop` 和
  `/sys/bus/usb/devices/%s:%d.0/usbip_sockfd` 等本地 USB/IP 导出证据。
- 字符串显示它会通过 CAG/HTTP 参数连接云端，处理 `importUsbDevice`、设备状态、异步
  URB（bulk/interrupt/iso）和 `usbip_status`；这与 Linux `usbip-host` 的职责对应，
  但不意味着必须携带它的安全、QoE、trace 或专有策略实现。
- `usbip.conf` 的 `DENY2_1=01,,`、`DENY2_2=e0,,`、`DENY2_3=03,01,` 和 `DENY3_1=09,,`
  分别覆盖通信/网卡类、无线类、Boot HID 及 Hub；错误字典还明确提示“不建议重定向鼠标
  或键盘”。这些是厂商策略，不是 USB HID 协议限制。本项目按用户目标不复制这些 deny
  规则，改为显式 busid、拒绝 hub/root hub，并允许用户有意选择 HID/存储设备。
- 官方 `monitor_qoe.sh`/`monitor_sohosdk.sh` 会检查并重启 `usbredirect`，还会为其设置
  SUID/权限；这部分与 USB/IP URB 数据面无关，已从 Debian 包和 systemd 设计中排除。

因此当前 Linux 结构是：

```text
显式 local_devices busid
  -> usbip-host（内核绑定/URB）
  -> 普通 usbipd 或已验证 backward proxy
  -> 云端 USB/IP peer
  -> 云端 usbhid/usb-storage
```

在 `dummy_hcd` 隔离测试中已经实际走通上述 `usbip-host -> usbipd -> vhci-hcd`，没有
解绑物理设备；真实云端只剩下把授权 JWAE/SCG/CAG 会话提供的传输端点接入这一标准数据面，
不能用本地测试结果冒充厂商会话已经完成。

## 28. 真实 Chrome 当前下载包的再次反向验证

本次没有只依赖历史保存文件，而是通过本机可见的真实 Chrome 页面刷新
`https://soho.komect.com/clientDownload`，在页面上下文读取当前下载 API。接口返回的
UOS AMD64 标识仍为 `ad2bcdde85d84d6a`；随后从该真实入口下载并解压了
`CMCC-JTYDN-UOSx86-2.23.1.deb`。

当前包的 `Package`/`Version` 为 `cmcc-jtydn 2.23.1`，且下列关键文件与先前解析样本
逐字节相同：Electron `resources/app.asar`、`chuanyun-redirect`、`chuanyun-view`、
`libchuanyun_usbip.so`、ZTE `usbredirect` 和 `libvdconn.so.1.0.0`。因此本轮真实 Chrome
验证没有发现新的 USB 或画面协议实现；此前关于客户端行为的结论仍然有效。

当前包仍清晰地分成两套实现：

1. UOS 公共 SDK 侧的 `chuanyun-redirect`/`libchuanyun_usbip.so`，通过 `libusbipd`、
   `libudev`、`libjwae` 提供厂商连接包装；
2. ZTE 侧的 `usbredirect`，直接依赖 `libzxsecurity.so`、`libqoelog.so`、
   `libusbtrace.so`、`libusbMsgSR.so.1` 等安全、QoE、trace 和设备消息库。

其 `usbip.conf` 当前仍把通信类、无线类、Boot HID、Hub、音频/虚拟设备列入策略过滤，
并将 USB 磁盘重定向作为独立策略开关。这些过滤和依赖属于官方策略/监控控制面，不是
USB/IP、HID、Mass Storage 或 SPICE 线协议的必要部分，所以没有带入清洁 Debian 包。

## 29. Debian 实机登录与真实云会话验证

在 Debian trixie x86_64 物理机上运行真实 Chrome 下载的 UOS AMD64 客户端
`cmcc-jtydn 2.23.1`，用户完成二维码登录后，客户端列表返回一个运行中的
`zte-cloud-pc` 云电脑。为避免官方包自带的旧版 glibc/mount 库覆盖系统库，启动时使用
系统库优先、厂商 SDK 库补充的 `LD_LIBRARY_PATH`；这是运行环境兼容性处理，不是把官方
安全服务带入清洁包。

首次点击“连接”时，官方连接控制面成功取得连接字符串并启动 `uSmartView_VDI_Client`，
但该 ELF 因 Debian 缺少公开 Qt5 Multimedia/Concurrent 运行库以退出码 127 结束。补齐
`libqt5multimedia5` 和 `libqt5concurrent5t64` 后重启客户端，第二次连接成功，真实窗口
显示云电脑 Windows 锁屏画面；截图保存在 `build/cmcc-smartview.png`（仅作本地证据，
不包含认证材料）。

这次真实会话给出了此前静态分析缺失的画面和输入证据：

- `client.log` 记录 `display-2:0` 的 SPICE display stream、H.264 IDR 帧和 FFmpeg 解码，
  实测分辨率为 `1920x1080`；同一 viewer 还建立 `cursor-4:0` 光标通道。
- `inputs-3:0` 输入通道初始化成功，服务器声明支持 `support_mouse_move=1`；向远端窗口
  发送无害的 Shift 按键后，日志记录了按下/释放和对应扫描码，证明键盘事件已经进入
  viewer 的远端输入路径。鼠标移动能力由同一输入通道和服务器能力标志确认，未执行远端
  点击或任何有副作用操作。
- viewer 监听本地 `51000`，并通过 CAG 建立云端传输；这与连接参数中的本地代理端口和
  SPICE/ICE 设计一致。画面、光标、键鼠并不是 USB/IP 设备转发。

## 30. 真实会话中的 USB 通道交叉验证

连接画面成功后，ZTE viewer 的 `usbMsgClient` 读取了 `--usb-redirect`、磁盘重定向和打印机
策略，并调用了 `auto redirect usb`。随后日志出现以下可审计的控制/数据面证据：

1. viewer 尝试初始化厂商 USB IPC 的本地端口范围 `3240`–`3250`，对应官方
   `usbredirect`/USB-IP 兼容控制面；本次没有启动官方 `usbredirect` 服务，因此这些早期
   本地端口探测失败，避免了实体设备被自动解绑。
2. viewer 随后在本地 `51000` 代理上收到 USB IPC 注册头，字段包含 `port=3246`、`type=2`
   和 `linkSource=117`，并明确标记 bandwidth-control link type 为 `usb`。
3. viewer 为该 USB link 创建 CAG out-of-band proxy；当前网络配置关闭 UDT/QUIC 后回退
   TCP，建立虚拟通道并把目的地指向云端 `3246`。这与 Windows 驱动和静态 ELF 中发现的
   USB/IP common header、DEVLIST/PRE_IMPORT/IMPORT 方向相互印证。

本次验证刻意没有把实体键盘、鼠标、摄像头或蓝牙设备导出；`usbip port` 保持空，物理
设备仍由原生驱动占用。因此这里验证的是“官方真实云会话确实携带独立 USB/IP 风格通道”，
不是宣称已完成实体设备到云端的导入。清洁 Debian 包的实体 USB 路径仍采用已通过
`dummy_hcd` 隔离回归的标准 `usbip-host -> usbipd -> vhci-hcd`，待后续将授权会话代理接入
这个标准数据面。

## 31. 真实验证对清洁适配边界的结论

真实画面首帧和键鼠输入已经可以复现官方 Linux 客户端的协议分层：

```text
登录/控制面 -> 厂商授权连接字符串
             -> uSmartView / 定制 SPICE display + input + cursor
             -> 独立 USB IPC / USB-IP 风格 out-of-band link
```

所以清洁包继续不携带 `libzxsecurity`、`libqoelog`、`libusbtrace`、官方
`usbredirect`、QoE/trace/monitor 脚本和 root 策略服务。它们不是画面 H.264 解码、SPICE
输入通道或 Linux USB/IP URB 承载的必要条件；清洁包只保留标准协议实现、显式设备选择、
隔离测试和默认关闭的 systemd 单元。官方客户端本次运行期间产生的认证 token、密码、
设备序列号和云端地址只留在本机运行日志，未写入本报告、源码或 Debian 包。

## 32. 真实 USB 导入的本地 IPC 和云端代理边界

为验证 USB 行为，本轮先在 loopback 上运行了一个只读实验监听器，再启动官方 viewer；
监听器不枚举 USB、不回复导入命令、不保存载荷，只统计连接长度并计算摘要。之后单独
启动官方 `usbredirect` 做一次真实会话交叉验证，实验结束后立即停止并恢复了摄像头的
`uvcvideo` 绑定。

### 本地 viewer IPC

`usbredirect` 的 `UsbredirectAsyCall` 符号和真实捕获相互印证：本地消息首部是 16 字节、
小端的四个 `uint32`：

```text
+0x00  cmdType
+0x04  clientSocket
+0x08  clientPid
+0x0c  msgLen
```

真实 viewer 注册前缀的形状是 `cmdType=1`、其余字段按本地连接状态填写；在同一进程中，
`cmdType=1` 分支调用设备列表处理，响应使用 `cmdType=2` 和 16 字节头加设备列表。官方
日志曾记录一次 4 设备列表总长度 5168 字节，但本报告不保存设备列表原始字节、PID、
地址或认证信息。

独立运行的官方 `usbredirect` 绑定 loopback `3240`；viewer 会在 `3240`–`3250` 范围内
探测可用控制入口。真实云会话日志出现的 `port=3246/type=2/linkSource=117` 是会话
link 的路由元数据，因此不能用“本机能连 3246”替代登录和 JWAE/SCG 授权。

### 云端 USB 数据面

官方 `sendProxyMesgHead` 函数在连接云端 USB endpoint 后先发送固定 0x74 字节的代理前置
头；反汇编可确认其包含版本/类型、端口和会话目的地字符串字段，但这些字段来自当前
授权会话，不能硬编码。随后真实导入日志出现：

```text
厂商会话代理前置
  -> USB/IP op_common
  -> DEVLIST request/reply
  -> IMPORT request/reply
  -> stub_rx/stub_tx
  -> control/bulk/interrupt/iso URB
```

该链路最终成功导入了一台摄像头，并出现标准 USB/IP 设备刷新、接口设置和 URB 处理；这
是对“云端 USB 数据面可承载 Linux USB/IP”的真实证据。因为官方自动策略把摄像头选为
候选而拒绝键盘/鼠标，本轮没有把实体键鼠或存储用于云端导入。

### 对 Debian 适配的影响

清洁包只实现可审计的标准部分：显式选择 busid、`usbip-host`/`usbipd`/`vhci-hcd`、
`usbhid`/`usb-storage` 和已通过 dummy_hcd 的 HID+Mass Storage 回归；不复制官方
`usbredirect` 的 SUID、自动策略、QoE/trace、监控和安全库。3246 反向桥仍保持显式关闭，
因为云端代理前置的会话字段必须由合法控制面提供，不能凭静态样本猜测。

## 33. 真实官方客户端的隔离虚拟 U 盘导入

为避免影响本机实体输入设备，本轮使用 `dummy_hcd` + ConfigFS 只创建了一台虚拟
Mass Storage 设备（VID:PID `1d6b:0105`，bus ID `21-1`），并在 mount namespace 中只
向官方 `usbredirect` 暴露该设备节点。官方 viewer 重新连接后，脱敏日志形成了完整链路：

```text
viewer -> loopback usbredirect IPC(3240)
       -> device 21-1 import request
       -> connect 127.0.0.1:<session-port>
       -> 0x74-byte vendor proxy preamble
       -> private import message
       -> USB/IP op_common
       -> DEVLIST -> IMPORT -> stub_rx/stub_tx -> URB
```

官方 `usbredirect` 明确报告：成功连接会话端点、发送代理头和导入消息，随后收到
`op_common`、`DEVLIST`、`IMPORT`，并启动 `stub_rx`/`stub_tx`；viewer 收到虚拟 U 盘
`result=0`、`redirectStatus=2`，随后移除失败提示。这是目前最强的真实云端 USB 转发
证据。该虚拟 gadget 对 Microsoft OS 字符串描述符请求返回 STALL，属于测试设备描述符
能力差异，不是代理握手失败。

清洁包 0.2.46 只加入了经过验证的 0x74 字节前置头序列化函数，且不默认调用它；函数
不保存或生成登录凭据，也不实现私有 0x500 导入消息。后者和 JWAE/SCG/CAG 建链仍必须由
合法会话控制面提供，因此清洁包仍不会宣称脱离官方登录控制面即可直连云端。

## 34. 私有导入消息的静态边界

进一步反汇编 `sendImportMsgUnified` 可确认：代理前置成功后，官方在某一分支分配并发送
固定 `0x500` 字节消息，消息头包含 `2 / 0x2f0 / 0x13`，并填入设备结构中的 bus/dev、
接口能力、视频/流能力和会话字段；随后再发送一个 16 字节的 `usbip_xmit` 请求并等待
导入回复。这个消息不是标准 USB/IP `op_common`，且其中的结构字段来自当前登录会话和
设备状态。

因此清洁包只实现了可以独立验证的 0x74 字节头序列化和标准 USB/IP 数据面，没有把这段
0x500 私有结构按猜测写入运行时。这样既避免了把认证/监控代码带入 Debian，也避免在
字段含义未完全确认时向云端发送不兼容或越权报文。

## 35. `chuanyun-redirect` 前端与 `libusbipd` 配置边界

本轮继续对官方 UOS AMD64 `chuanyun-redirect` 和 `libusbipd.so` 做静态交叉验证，得到一
个更清晰的分层：`chuanyun-redirect` 不是 USB/IP 数据面本身，而是 root 运行的策略/IPC
前端；它通过 SysV 消息队列接收上层命令，维护本地设备表，然后把配置 JSON 交给
`cdp_usbip_start_service(g_json, NULL)`。已确认的消息类型行为为：

- `1`：更新 USB 服务配置并启动/等待服务；
- `2`：停止并清理 USB 服务；
- `3`：按 busid 映射设备；
- `4`：按 busid 解除映射；
- `9`：重新枚举并向主程序发送设备列表。

它还会把设备状态制作成 JSON 回报、按名称匹配 deny 列表、写入
`/etc/udev/rules.d/99-usb-permissions.rules`，并在退出时做清理。这些行为属于厂商策略、
权限和监控控制面，不是 HID、Mass Storage 或 USB/IP URB 承载的必要条件，所以没有带入
清洁 Debian 包。

### 官方 USB 服务 JSON

`libusbipd.so` 的 `rh_server_config_update` 直接解析并校验下列字段：

```json
{
  "config_version": 1,
  "maxWidth": 1920,
  "maxHeight": 1080,
  "hotplug_support": true,
  "use_scg": true,
  "port": 3246,
  "ip": "127.0.0.1",
  "ALLOW_LIST_1": [],
  "DENY_LIST_1": [],
  "ALLOW_LIST_2": [],
  "DENY_LIST_2": [],
  "vimId": 0
}
```

其中四个 allow/deny 列表由官方内部解析器转换为设备过滤表；`maxWidth`、`maxHeight` 是
能力配置，不是 USB/IP 标准字段。官方库还把 `ip`、`port`、TLS/SCG 开关和列表解析结果
保存到内部 server config，再启动标准 USB/IP 请求处理器。因此这份 JSON 只能作为官方
库行为记录，不能独立生成合法云会话或替代登录授权。

### JWAE 启动结构

`libusbipd.so` 导出的 `api_android_start_jwae_service` 会把参数整理成约 `0xd8` 字节的
内部配置后调用 `jwae_start`；静态可确认的字段包括模式/版本整数、会话相关字符串、
`127.0.0.1` 回环端、默认 `10800` 本地服务端口和目标地址字符串。真正的 token、密码、
SCG/CAG 地址及会话字段由上层连接控制面在运行时提供，不能从安装包静态恢复。

这解释了当前清洁包的取舍：标准 `usbipd`/`vhci-hcd`、显式设备选择和 HID/storage
数据面已经可独立验证；官方 `libjwae`、安全库、QoE/trace、root 策略前端以及私有
`0x500` import 消息仍保持在适配边界之外。若未来由合法会话适配器提供已授权的目标端点
和 import payload，只需把该控制面接到现有标准数据面，不需要重新引入监控或安全守护进程。

## 36. 可选官方 USB ABI loader 的真实库加载验证

在不启动官方客户端、不读取或输出任何会话凭据的条件下，用官方 UOS 包外部的
`libchuanyun_usbip.so` 做了最小 `list` 验证。loader 显式预加载同包的 `libjwae.so`，并
使用惰性符号解析；这样 USB-only 的设备枚举不会在进程启动时强制解析
`libusbipd.so` 中仅供画面/编码路径使用的 `create_ffmpeg_encoder`。命令返回成功且无设备
时正常输出空列表，说明公共设备枚举 ABI 的加载边界在 Debian x86_64 上可达。

这项验证仍然不等于云端互通：没有会话时没有云设备可列出，也没有启动私有 JWAE/SCG
建链、导入消息或任何官方 root 策略前端。惰性加载只改进了“操作者显式提供官方库时的
USB ABI 兼容性”，清洁 Debian 包仍不包含 `libchuanyun_usbip.so`、`libusbipd.so`、
`libjwae.so`、视频编码器、安全、QoE、trace 或监控组件。

## 37. 已登录官方 UOS 会话的画面与 USB 边界复核

在用户已登录并点击连接后，官方 UOS 客户端从保存的会话恢复了一个真实云桌面。脱敏后的
运行证据如下：

- 官方 viewer 日志持续出现 `display-2:0` 帧计数和 H.264/SPICE 解码、渲染统计，分辨率
  配置为 1920×1080；
- 鼠标移动、按下、释放分别进入 `spice_gtk_mouse_position`、
  `spice_gtk_mouse_button_down/up`，键盘/鼠标空闲检测也由 viewer 完成；
- 这进一步确认画面和输入共享一个 SPICE/VDI 语义层：画面是压缩帧/显示通道，键鼠是
  输入事件，不依赖 USB/IP 把本机键盘或鼠标伪装成云端 USB 设备；
- 同一会话的 USB 模块收到自动 USB、TWAIN、网络打印机等控制调用，并向单独的会话端点
  建立连接；端口是会话运行时字段，不把它当作固定公网端口或登录入口。

对官方本地监听的 51000 端口运行了 8 秒的标准 `ydyun-spice-viewer` 短测。该连接没有
形成第二个持续 SPICE 会话，原官方连接保持不变；因此 51000 是厂商 VDI/控制封装入口，
不是清洁 viewer 可以直接替代官方登录和会话协商的裸 SPICE 端口。清洁包中的标准 SPICE
viewer 仍然适用于用户明确提供的标准 SPICE endpoint，但不伪装成官方控制面客户端。

本次复核没有导出实体 USB，也没有把官方的 QoE、策略、日志上报或安全库复制进清洁包。
结论是：Linux 侧画面/键鼠转发可由标准 SPICE 数据面承载；官方云端接入仍需合法控制面
提供会话协商和运行时端点；USB 存储/HID 则沿独立 USB/IP 会话通道，不能用画面端口替代。

## 39. KDE Wayland KScreen 实机收敛

在 Debian trixie Plasma Wayland 实机上，补丁版 `spice-vdagent` 启动日志确认：

```text
KScreen current Virtual-1 1920x1080+0+0
```

实现过程中确认 `kscreen-doctor` 即使设置 `TERM=dumb` 仍会输出 ANSI CSI 颜色序列；如果
直接按可见文本解析，会错误地回退到 Mutter。最终实现对 KScreen 查询结果先剥离 CSI 序列，
并为 Qt 子进程显式选择 Wayland 平台，因此 KDE/KWin 不再依赖不存在的
`org.gnome.Mutter.DisplayConfig`。这一改动只处理 Wayland 输出模式查询/设置，不改变
标准 SPICE 通道、输入、声音或 USB/IP 数据面。

## 38. 全屏/还原与 KDE Wayland 分辨率适配

对官方客户端多次点击全屏/还原后，日志形成稳定的尺寸序列：窗口最大化过渡先发送
`1920x1014`，真正全屏发送 `1920x1080`，还原发送 `1024x600`。每次变化都伴随
`display_handle_surface_destroy/create` 和新的视频流；流在重建后继续收到 IDR 帧，说明
客户端显示数据面本身没有因为窗口操作失效。

当前 Debian 虚拟机是 Plasma Wayland。原版 `spice-vdagent 0.22.1` 的实现一方面文档和
X11/XRandR 路径面向 X11，另一方面在 Wayland 下尝试 GNOME 专用的
`org.gnome.Mutter.DisplayConfig`。远端日志反复出现：

```text
failed to call GetCurrentState from mutter over DBUS
proxy is for the well-known name org.gnome.Mutter.DisplayConfig without an owner
No guest output map, using output index as display id
```

KDE KWin 不拥有该名称，所以还原请求没有被正确应用；QXL/KScreen 最终使用了已公布的
`1024x768` 模式。全屏 `1920x1080` 因为是现有 DRM 模式，能够正常生效。

仓库的 Wayland 补丁把 `spice-vdagent` 的 Wayland monitor set/query 分支改为无 shell 调用
`kscreen-doctor`，从 KScreen 已公布模式中优先选择精确分辨率，否则选择最接近模式，并把
当前几何回报原有 agent。补丁只改变显示模式适配，不引入官方安全、QoE、trace、监控或
第二个 virtio agent 守护进程；单 QXL 输出已覆盖，多输出映射仍待继续验证。
