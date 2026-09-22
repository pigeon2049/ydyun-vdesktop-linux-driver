# 官方 Linux 客户端 ABI 静态证据

本文件记录从官方麒麟 AMD64 客户端中的
`libspice-client-glib-2.0.so.8.8.2` DWARF 调试信息得到的静态结果。
只读解析 ELF/DWARF，没有执行官方 ELF，也没有把闭源库复制到 Debian
生产包。

## 已确认的连接入口

DWARF 给出了以下 C ABI 形式的原型（x86_64 System V ABI）：

```c
typedef struct _SpiceConnection SpiceConnection;

SpiceConnection *connection_new(void);
int connection_connect_client(SpiceConnection *conn);
int connection_connect_session(SpiceConnection *conn);
int connection_connect_set_config(SpiceConnection *conn, const char *json_str);
int connection_connect_set_session_id(SpiceConnection *conn,
                                      const char *session_id);
void connection_destory(SpiceConnection *conn); /* 官方拼写 */
```

其中 `connection_connect_set_config()` 的第二个参数不是结构体指针，
而是以 NUL 结尾的 JSON 字符串；`connection_connect_set_session_id()`
的第二个参数也是字符串。两个 setter 的返回类型均为 `int`。

## `SpiceConnection` 的静态布局

DWARF 报告 `_SpiceConnection` 大小为 104 字节，字段如下。这里仅用于
验证调试信息和调用边界，不能作为外部程序直接构造对象的建议；对象内含
GLib `GObject`、私有会话指针和内部列表，必须由官方构造函数负责分配。

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| 0 | 24 | `GObject object` |
| 24 | 8 | `SpiceSession *session` |
| 32 | 8 | `GMainLoop *main_loop` |
| 40 | 8 | `SpiceChannel *main` |
| 48 | 4 | `int channels` |
| 52 | 4 | `int disconnecting` |
| 56 | 4 | `int disconnect_reason` |
| 60 | 4 | `int connection_state` |
| 64 | 4 | `int connection_reconnect_cnt` |
| 68 | 4 | `int connection_exit_code` |
| 72 | 4 | `int has_connected` |
| 76 | 4 | `int return_val` |
| 80 | 8 | `char *json_config` |
| 88 | 8 | `char *session_id` |
| 96 | 8 | `GList *va_param_list` |

## 对 Linux 适配的影响

这证明官方 Linux 客户端不是把 Windows `.sys` 驱动搬到 Linux，而是由
用户态连接对象接收认证阶段生成的 JSON 和 session ID，再建立厂商 SPICE
会话。它为以后编写“最小画面客户端”提供了明确的调用边界：

```text
getFirmAuth JSON + session ID
        -> connection_new()
        -> connection_connect_set_config()
        -> connection_connect_set_session_id()
        -> connection_connect_client/session()
        -> display/input channels
```

但仍有三个不能从该证据推断的部分：

1. JSON 字段的完整语义、版本兼容和 token 生命周期；
2. `connection_connect_client()` 与 `connection_connect_session()` 的选择条件；
3. JWAE/SCG 的认证、TLS/KCP/厂商扩展和实际画面帧解码。

因此当前生产包继续只实现标准 Linux USB/IP；标准 SPICE GTK viewer 保持
实验性。不能仅凭这些原型猜测 `ctypes` 调用，也不能绕过官方认证或删除
画面传输所必需的 TLS/会话安全部分。

## 配置对象中的核心字段与采集字段

对同一官方 `chuanyun-view` ELF 的 DWARF 类型信息还原出以下配置对象。它们是
官方内部 C++ 对象，不是本项目要复制的 ABI；这里的价值是区分哪些策略属于 USB/
画面连接本身，哪些属于监控、探测或外围功能。

`ConnectionConfig` 的静态字段为：

| 字段 | 作用判断 |
|---|---|
| `strOsType`、`strSDKVersion` | 会话元数据 |
| `stEncodingModeThresholdConfig` | 编码模式阈值 |
| `stVideoEncodingParamConfig` | 帧率、码率、QP 等画面编码参数 |
| `stNetworkAdaptationConfig` | 传输协议和码率联动 |

`CommonConfig` 中可见的字段包括：

| 字段 | 适配分类 |
|---|---|
| `usbPortRedirectEnabled` | USB 转发核心开关，不能误删 |
| `networkTransportProtocol` | 画面/会话传输策略，需按真实会话保留 |
| `sdkPointCollectPeriod`、`sdkPointReportPeriod`、`sdkPointEnbaled` | SDK 质量/运行数据采集，Linux 精简版排除 |
| `publicProbeTimeout`、`publicProbeDomain` | 公网探测/网络质量探测，Linux 精简版排除 |
| `deviceDiskMappingSwitch`、`usbCameraPattern`、`miPiCameraSwitch` | 外围设备/摄像头策略，当前 USB storage/HID 目标排除 |
| `clipboard*` | 剪贴板/文件拖放外围能力，当前驱动不实现 |

这为包审计提供了字段级依据：保留标准 USB/IP、HID、存储和必要的会话传输边界，
不移植 SDK 采集、公网探测、摄像头、磁盘映射和外围监控对象。

## 官方 USB 压缩封装的静态边界

麒麟客户端的 `libusbipd.so` 带有 DWARF 类型和可读的本地符号。静态反汇编确认
`network_send_data_with_compressed`/`network_recv_data_with_compressed` 使用一个
9 字节网络顺序封装：

```text
u8  type
u32 uncompressed_size
u32 compressed_size
u8  payload[compressed_size]
```

`type=0` 时 payload 是原始字节；`type=1` 时 payload 是 raw LZ4 block，接收方按
`uncompressed_size` 解压并校验输出长度。官方发送器对很短数据（静态门槛为 512 字节）
直接使用原文，对压缩后没有变小的数据也回退到 `type=0`；零长度数据走无封装发送路径。

当前 Linux 桥只实现了安全的封装转换：`compression=off` 保持原始字节；显式设置
`compression=lz4` 时，本地标准 USB/IP 数据使用 type-0 封装，远端 type-0/type-1
封装被还原为标准 USB/IP 数据。纯 Python LZ4 解码器对长度、偏移、输出上限和类型做了
校验，没有引入官方闭源库。由于尚未取得真实云会话，默认仍为 `off`，且没有实现
自动探测、认证、TLS、KCP 或未知压缩变体。

另一个关键静态证据是 `network_send_data()`/`network_recv_data()` 会根据连接对象中
的压缩开关（DWARF/反汇编可见的偏移 `+0xe30`）选择普通 stream 或上述封装；USB/IP
控制面和 `send_xfer_data` 等传输调用均经过这两个入口。`network_init_compress_link()`
只负责打开压缩 stream 缓冲区，接收端会先读取 9 字节头，再将解压结果写入该缓冲区，
因此封装是 USB/IP 字节流的外层传输包装，不改变 DEVLIST/IMPORT/URB 的字段布局。

## 画面与输入回调边界

官方 `chuanyun-view` 的未剥离符号和字符串进一步确认了标准 SPICE 下层的两组回调：

| 方向 | 可见 API/回调 | 作用 |
|---|---|---|
| 画面 | `displayPrimaryCreate`、`displayInvalidate`、`displayPrimaryDestroy` | 创建/更新/销毁显示 surface |
| 画面 | `displayOpenGlInvalidate`、`displayMark`、cursor 回调 | GPU/脏区标记和光标更新 |
| 输入 | `spice_inputs_channel_position` | 鼠标绝对位置 |
| 输入 | `spice_inputs_channel_button_press/release` | 鼠标按键 |
| 输入 | `spice_inputs_channel_key_press/release` | 键盘扫描码 |
| 输入 | `spice_inputs_channel_set_key_locks` | Num/Caps/Scroll 锁定状态 |

这些回调说明画面与键鼠并非由 Windows USB 驱动完成：画面由 SPICE display channel
更新，键鼠由 SPICE inputs channel 上行。Linux 标准 viewer harness 已覆盖这一公开
边界；仍缺少的是官方 JWAE/SCG 建立的真实 channel，而不是 GTK 或 `usbhid` 层。

## 官方 USB 主链路握手边界

官方麒麟 `libusbipd.so` 的静态证据还原出一个不能与本机 3246 端口混淆的入口。JWAE/SCG
先提供内部网络对象 `g_link`，随后 `handle_usbip_link_init` 在该链路上发送：

```text
USB/IP common header: version=0x0111, code=0x8008, status=0
```

并等待 `code=0x0008/status=0` 的响应。随后由对端驱动命令循环；官方
`handle_usbip_command()` 读取对端发来的请求，至少处理：

| 请求 | 响应/后续 | 静态含义 |
|---|---|---|
| `0x8005` `OP_REQ_DEVLIST` | 本端回 `0x0005` + 设备数量 | 标准 USB/IP 设备枚举 |
| `0x8009` `OP_PRE_REQ_IMPORT` | `busid[32]` 后进入 import | 厂商主链路上的预导入 |
| 标准 import 阶段 | `0x0003` + 总长度 0x140 的设备信息帧 | 建立 libusb forwarding |

对官方 `handle_usbip_pre_req_import()` 的反汇编进一步确认：它先从已经建立的
`g_link` 读取固定 32 字节 bus ID，再回送 `0x0009` 公共头。静态分支中可确认的状态
包括：设备不存在时 `status=4`、端口被禁用时 `status=2`、内部链路/发送失败走错误
清理；设备存在且允许时进入 `handle_usbip_req_import_2()`。后者向后续 forwarding
连接发送 `0x0003`、总长度 `0x140` 的 import reply，并等待对端的 `0x0003/status=0`
确认，然后启动 USB forwarding。这个方向说明官方云端是控制方，Linux 客户端是设备
列表/预导入/数据 forwarding 响应方；它不能由本机单独伪造出合法 JWAE/SCG 链路。

### `libusbipd.so` 的 USB/IP 控制帧结构（DWARF 复核）

对官方 UOS `libusbipd.so` 的 DWARF 类型信息做了只读提取，确认标准 USB/IP 控制面的
关键尺寸。该结果用于校正帧边界，不表示把官方库链接进 Debian 包：

| 类型 | 大小 | 关键字段 |
|---|---:|---|
| `usbip_usb_device` | 312 (`0x138`) | `path` 偏移 0、`busid` 偏移 256、`busnum` 偏移 288、`devnum` 偏移 292 |
| `usbip_usb_interface` | 4 | class/subclass/protocol 各 1 字节 |
| `usbip_op_common` | 8 | version/code/status 控制头 |
| `usbip_op_import_request` | 32 | NUL 结尾的 busid 字段 |
| `usbip_op_import_reply` | 312 (`0x138`) | 以 `usbip_usb_device` 作为 body |
| `usbip_op_devlist_reply` | 4 | `ndev` 设备数量 |

因此 PRE_IMPORT 成功后的 `OP_REP_IMPORT` 总长度是 8 字节 common header 加 312 字节
device body，即 320 字节 (`0x140`)；文档中的“`0x0003 + 0x140`”应理解为
“code 为 `0x0003` 的回复、总帧长为 `0x140`”，而不是把 312 字节 body 再额外加
一次 0x140。这个边界与 `libusbipd.so` 的 `handle_usbip_req_import_2` 静态行为一致。

当前生产代码仅实现无副作用的 link probe、本地 USB/IP/VHCI 数据面和离线协议验证；
没有伪造云端认证、JWAE/SCG 建链或 URB 数据转发。

`network_send_data`/`network_recv_data` 位于该链路字节流外层，依据连接对象的压缩开关
选择原文或 9 字节 raw/LZ4 封装。`android_usbipd_start(json,on_init)` 反而启动本机
RemoteHub/libusb 导出服务，两者职责不同。公开包装层的准确声明是
`int cdp_usbip_start_service(const char *json, void (*on_init)(int))`：第二参数是可选
回调指针，不是 `int *` 输出地址；官方 `chuanyun-redirect` 传入 `NULL`。精简 loader
同样传 `NULL`，因此不会进入状态/监控回调路径。

由于 `g_link` 的 socket、认证、TLS/KCP 和会话生命周期由闭源 JWAE/SCG 管理，以上是
协议边界证据而非可直接拨号的 ABI。生产适配不能把 `0x8008` 当作 3246 TCP 服务，也
不能凭对象偏移猜测 `libjwae` 调用；当前 Debian 包保持离线验证和标准 USB/IP 实现，
将真实主链路适配留给获得合法会话参数/样本后的独立阶段。

## 官方 Linux `chuanyun_api.h` 的公开入口

官方麒麟/UOS 包实际随附了头文件
`ccsdk/uos/include/chuanyun_api.h`，因此下面这组 Linux C ABI 不再只是符号推测：

```c
int chuanyun_init(chuanyunInitParam_t *param);
int connectVm(const char *vmid, const char *username,
              const char *auth_code, const char *biz_code);
int disconnect(const char *vmid);
int deInit(void);
```

`chuanyunInitParam_t` 的字段顺序为服务 IP/端口、日志服务 IP/端口、终端 SN、终端
类型，以及连接状态、首帧、网络状态和桌面事件回调。Linux 头文件把后三类回调明确
分开；精简适配器只保留连接状态和首帧回调，日志服务地址、网络质量采集和桌面事件
回调均传空，不把监控链路接入默认 USB 服务。

Windows 头文件还把 `DesktopStreamBean` 明确为六个 `int`：帧率、码率、宽高、QP、
丢帧率；`UpScreenStreamBean` 为上行帧率、码率、丢帧率。它证明画面参数是独立的
控制面结构，不是 USB HID 或 USB/IP 数据。实际 display/input 通道仍由官方闭源库
负责，不能用这些参数结构替代真实视频帧协议。

当前包中的 `ydyun-chuanyun-session` 只做 `dlopen` ABI 适配：不携带官方
`libchuanyun.so`，不自动登录，不打印 auth code/token/错误字符串，不作为 systemd
服务启动。操作者显式提供已授权的官方库和会话配置后，它才会调用上述公开入口；这
为真实会话验证提供了精确边界，同时保持默认 Debian USB 驱动不依赖闭源运行时。

## 官方 JWAE `StartConfig` 的静态 ABI 证据

官方 UOS `chuanyun-view` 保留的 DWARF 给出了 `StartConfig` 的完整 x86_64 布局，总大小为 112 字节。`QSpiceConn::startSCG()` 先调用 `jwae_set_token(const char *)`，再把该结构地址传给 `jwae_start(StartConfig *)`：

| 偏移 | 字段 | 类型 |
|---:|---|---|
| 0 | `mode` | `int` |
| 4 | `auth_type` | `int` |
| 8/16/24/32/40 | TCP/UDP IPv4/IPv6 与 client 地址 | `const char *` |
| 48/50/52/54/56/58 | TCP/UDP IPv4/IPv6、client/reserve 端口 | `uint16_t` |
| 64 | `config_path` | `const char *` |
| 72 | `ping_ip` | `const char *` |
| 80 | `log_config` | `SdkLogConfig` |

`SdkLogConfig` 为 32 字节，包含日志路径、级别、大小和文件数；`ConnInfoNew` 为 80 字节，包含发送/接收速率、RTT、丢包率和 cloud-peripheral 计数。后者是质量监控结构，不属于画面或 USB 数据面；当前精简适配器不定义、不读取、不上报这两个结构。

反汇编确认官方 viewer 在当前包中把 `auth_type` 设为 2，把 TCP/UDP 地址都指向同一会话地址，IPv6/client 地址置空，`client_port` 为 10800，并把动态控制地址放入 `ping_ip`。这些是某一版本 viewer 的静态默认，不应硬编码为云端协议常量；实际参数仍应来自授权会话响应。

因此 `jwae_start` 是厂商隧道入口，不是标准 SPICE 或 USB/IP 入口。Linux 包只保留公开 `chuanyun_api.h` loader 与标准 USB/IP/SPICE 下层，不复制 `libjwae.so`，也不把质量监控结构带入生产代码。

官方 USB 动态依赖也必须单独看待：`libchuanyun_usbip.so` 的 `DT_NEEDED` 包含
`libusbipd.so`、`libudev.so.1` 和 `libjwae.so`，而 `libusbipd.so` 继续依赖
`libjwae.so`。这说明 `cdpusblib_attach_device()` 等导出是厂商运行时的本地包装，
不是一个可脱离 JWAE/SCG 的通用 Linux USB 驱动 ABI。当前 Debian 包的 loader 仅接受
操作者显式指定的外部库，并且默认不加载、不安装这些闭源依赖。

同一 `libchuanyun_usbip.so` 还导出两个面向存储映射的 C 函数；对其反汇编确认参数是
单个 `const char *busid`，内部直接转发到 RemoteHub 的 map/unmap：

```c
int cdp_usbip_drive_map(const char *busid);
int cdp_usbip_drive_unmap(const char *busid);
```

精简 loader 仅在操作者显式输入 `map BUSID` 或 `unmap BUSID` 时调用它们，不加载官方
deny-list、SysV 消息队列、udev 注入或状态/质量回调；这补齐了存储设备的公开映射入口，
但仍不替代建立 `g_link` 所需的合法 JWAE/SCG 会话。

官方 `ccsdk/uos/bin/chuanyun-view` 的 `DT_NEEDED` 和未剥离符号进一步证明了窗口层：
它直接链接定制 `libspice-client-glib-2.0.so.8` 与 `libjwae.so`，并在本地实现
`QSpiceWidget::displayPrimaryCreate/displayInvalidate`、
`QSpiceHelper::inputs_modifiers/main_mouse_update`、
`QSpiceConn::startSCG/StartCdpUSB`。这些符号属于原生 Qt viewer 和会话层，不属于
Linux 内核 USB 驱动；当前适配器只复用已验证的标准 USB/IP/Spice 下层，不复制该 ELF。

### viewer 私有 USB manager 的 ABI 限制

DWARF 记录的 `DeviceRedirectManager` 私有入口为：

```text
InitCdpUSB(std::string, std::string, int, std::string)
StartCdpUSB(std::string, std::string, int, int, int, std::string)
UninitCdpUSB()
StopCdpUSB()
```

反汇编可见 `StartCdpUSB` 会把设备容器和会话字段交给 `generateTargetJson`，再通过
内部 `MessageQueue::send` 传递；`InitCdpUSB` 还会读取 INI、解析 allow/deny 列表并
启动 Qt 消息线程。viewer 的 `UsbDeviceInfo` 大小为 420 字节，不能与公开 USB 库的
0x494 字节列表记录混用。当前 Linux loader 不调用这些私有入口，也不复刻其策略线程。

## 画面转发的标准 SPICE 消息边界

结合本地 SPICE 0.14.3 协议头和 0.15.2 参考实现，画面接收端至少要处理两条显示路径：

| 路径 | 服务端消息 | 作用 | Linux 端动作 |
|---|---|---|---|
| 主 surface/脏区 | `SPICE_MSG_DISPLAY_MODE=101`、`SURFACE_CREATE=314`、`DRAW_*`、`COPY_BITS`、`SURFACE_DESTROY=315` | 以 surface、矩形和绘制命令更新本地 framebuffer | 建立 surface，按命令更新纹理/像素缓存 |
| 视频 stream | `SPICE_MSG_DISPLAY_STREAM_CREATE=122`、`STREAM_DATA=123`、`STREAM_CLIP`、`STREAM_DESTROY` | 传输带 codec 的帧数据和目标矩形 | 交给 H.264/VP8/VP9/本地协商 codec 解码后合成 |

协议结构还明确了 `SpiceMsgDisplayStreamData` 的 `data_size + data[]` 可变尾部，以及
`SpiceMsgDisplayStreamDataSized` 的宽高、目标矩形和可变帧数据。光标另走 cursor channel，
不是 USB 设备数据。官方 viewer 的 `displayPrimaryCreate/displayInvalidate`、
`displayOpenGlInvalidate` 正好对应上述 surface/脏区入口；它们说明“画面转发”不是
`usbip` 或 Windows USB 驱动转发。

桌面键鼠在 inputs channel 上行，协议枚举和结构已确认：键盘使用
`SPICE_MSGC_INPUTS_KEY_DOWN/UP/SCANCODE`，鼠标使用 `MOUSE_MOTION`、`MOUSE_POSITION`、
`MOUSE_PRESS/RELEASE`；绝对位置结构为 `x/y/buttons_state/display_id`，键盘锁定状态由
`SPICE_MSG_INPUTS_KEY_MODIFIERS`/`SPICE_MSGC_INPUTS_KEY_MODIFIERS` 同步。Linux viewer
只需把本机窗口事件转换成这些 channel 消息，和 USB HID forwarding 是两条独立路径。

因此当前工程的实现分层是：厂商 JWAE/SCG 负责会话和定制 SPICE channel；标准 SPICE
harness 负责验证 display/input API；USB storage/HID 继续走标准 USB/IP。没有合法厂商
会话或抓包样本时，不能把这些标准消息直接连接到云端并宣称完成画面适配。

`linux/src/ydyun_spice.py` 现在还提供 `decode_inputs_message()`，对 inputs channel 的
键盘、扫描码、modifier、鼠标相对移动、绝对位置、按下和释放消息执行长度边界校验并
输出结构化字段；它只解析离线帧，不建立网络连接，也不模拟厂商认证。

该模块同时提供 `encode_inputs_message()`，可将 Linux 窗口事件编码成标准 SPICE
inputs-channel data frame。特别地，`SPICE_MSGC_INPUTS_KEY_SCANCODE=104` 按参考协议
编码为 1–2048 字节的可变长度 `Data`，不是固定 `uint32`；这支持扩展键、释放位和组合
扫描码序列。编码器只生成 channel data frame，不负责云端认证、TLS/KCP、channel
negotiation 或 socket 发送。

## 官方 viewer 的 USB 运行时边界进一步收敛

对官方 `ccsdk/uos/bin/chuanyun-view` 的动态符号和 `DT_NEEDED` 做了第二次交叉核对，得到
以下重要结论：

- viewer 导入的是定制 `libspice-client-glib-2.0.so.8` 提供的
  `spice_usb_device_manager_get/get_devices/get_devices_with_filter`、
  `can_redirect_device`、`connect_device_async/finish` 和
  `disconnect_device_async/finish` 等符号；这是 viewer 自己的 SPICE USB 管理层。
- viewer 本身没有导入 `cdpusblib_get_device_list`、`cdpusblib_attach_device` 等公开
  `libchuanyun_usbip.so` 符号，`DT_NEEDED` 也不包含 `libchuanyun_usbip.so`。所以公开
  USB C ABI loader 和官方 Qt viewer 的私有 USB manager 是两条不同运行时路径。
- viewer 直接使用 `msgget/msgsnd/msgrcv/msgctl` 和 `system`，字符串中还出现
  `DeviceRedirectManager`、`MessageQueue`、`scanUsbDevicesWithLibusb`。这对应本地设备
  扫描、策略队列和自动重定向控制面，不应作为 Debian 数据面 ABI 复刻。
- viewer 仍直接依赖 `libusb-1.0.so.0`，说明本地 USB 枚举与云端设备映射是分开的；远端
  USB 数据传输不能从 Electron 的本地状态扫描逻辑或 Qt manager 的私有队列推导。

因此，`ydyun-chuanyun-usb` 的定位必须保持为“操作者显式提供官方公开 C ABI 时的最小
兼容入口”，不能宣称它替代 `chuanyun-view` 的自动策略层。默认 Debian 包继续不加载
`libjwae.so`、不创建 SysV USB 策略队列、不执行 viewer 的 `system`/udev 控制路径；真正
的 storage/HID 数据面优先落在标准 USB/IP 和 Linux `usb-storage`/`usbhid` 上。

## JWAE/SCG 不是可复制的画面或 USB ABI

本轮对官方 `libjwae.so` 的导出与内部符号做了只读复核。可见的公开入口主要是
`jwae_set_token`、`jwae_start`、`jwae_stop`、连接信息和网络开关；内部还包含 DTLS、
trunk、side-channel、redirect-manager、monitor-server 及质量统计结构。这些对象共同
构成认证和隧道运行时，不能从 `strings` 或单个 `StartConfig` 偏移推导出稳定的跨版本
连接 ABI。

因此当前实现只保留两类可验证边界：标准 USB/IP/VHCI/Linux 类驱动，以及标准 SPICE
display/input 的离线解析和 GTK harness。真实画面仍需官方授权会话提供 vendor SPICE
channel；真实 USB 主链路仍需 JWAE/SCG 提供的 `g_link` 和会话样本。监控、QoE、trace、
side-channel 和安全策略组件不进入 Debian 包。

## ZTE 定制 SPICE ABI 的安全/监控排除

官方 `libspice-client-glib-zte-2.0.so.8.5.0` 的 `DT_NEEDED` 包含
`libzxsecurity.so`、`libzx_yuv2rgb.so`、多个 FFmpeg/OpenH264/dav1d 库和 ZIME
data-channel；未剥离符号还包含 `spice_gtk_get_display_frame_param`、
`spice_gtk_get_display_DMA_data`、`spice_inputs_*`、`avcodec_get_screen_to_client`。
这些是显示/输入数据面证据，但同一 ABI 还包含 `spice_gtk_send_qoeagent_msg`、
`spice_gtk_set_perfmon_log*`、`spice_gtk_insight_report`、网络质量、远程协助和安全
入口。

因此不能通过复制部分符号或替换 SONAME 来安全移植该闭源库。正式 Debian 包不携带该
库及其依赖；只使用系统 `spice-gtk` 的标准 display/input harness，等待真实会话样本
确认 vendor 扩展后再实现独立、可审计的解码适配。

## ZIME 传输层边界

ZTE 包中的 `libZIMEDataEngine.so` 导出数据 channel/stream、发送接收、事件回调和
stream 参数接口；其未剥离符号还显示 `ZIMEQuic`、`ZIMESctp`、FEC、证书查找/更新、
cipher preference、QoS、alarm 和 engine statistics。定制 SPICE 库通过这些接口承载
厂商数据通道，所以 `SpiceSession(host, port)` 不能替代官方 JWAE/SCG/ZIME 建链。

该库没有进入 Debian 包，也没有用 `dlsym` 猜测其 C++ ABI。当前可验证的部分仍然是
标准 USB/IP/VHCI、标准 SPICE display/input framing，以及显式输入的离线样本；真实
ZIME 字节和会话时序必须来自授权云端样本。

## 官方 viewer 的 URL 启动边界

对 UOS `libchuanyun.so` 的 `connectVm` 异步路径做了静态交叉验证：云端状态就绪后，
库内部拼接以 `./chuanyun-view spice://127.0.0.1:10800+` 开头的命令，并调用其系统
命令封装启动官方 viewer。`chuanyun-view` 的 `main` 将第一个位置参数交给
`QSpiceConn::parseURL`；该函数按 `://` 和 `+` 分隔连接地址及后续会话/config 字段。
后续字段可能承载认证或会话材料，不能写入日志或当作普通 shell 参数处理。

因此清洁 Linux 适配层新增了 `parse_viewer_connection_url()`，只接受有界的
`spice://host:port+...`/`spice-conn://host:port+...` 形式，校验主机、端口、控制字符和
字段数，并只提供脱敏摘要。它不解密、不建立 JWAE/SCG、不执行命令，也不复刻官方库的
`system()`、监控、QoE、策略队列或安全路径。真正授权会话仍须由外部官方运行时或未来
独立实现的会话适配器提供；Debian 包只使用标准 SPICE display/input 和 Linux USB/IP。

对 `libchuanyun.so` 的 `ConnectInfo::serialize` 还原得到以下公开符号级字段名和对象布局：

| 对象偏移 | 字段名 | 类型边界 |
|---:|---|---|
| `+0x20` | `traceId` | `std::string` |
| `+0x40` | `scAuthCode` | `std::string` |
| `+0x60` | `hostIp` | `std::string` |
| `+0x80` | `scgIp` | `std::string` |
| `+0xa0` | `scgIpv6` | `std::string` |
| `+0xc0` | `scgTcpPort` | `std::string` |
| `+0xe0` | `scgUdpPort` | `std::string` |

同一连接线程的拼接顺序静态可见为：命令前缀 -> operator 内部 `+0x258` 字符串 ->
`scgTcpPort` -> `scgUdpPort` -> `scAuthCode` -> `traceId` -> 两个线程内生成/传给
`InitNamedPipe` 的字符串。这里的“字段名”来自 `cereal` 序列化符号，“拼接顺序”来自
反汇编；没有把它们当成稳定的跨版本 URL 公开 ABI。尤其 `scAuthCode`、管道名和随机值
不能输出、持久化或交给 shell。清洁 parser 因此只报告 endpoint 和字段数量，后缀全部
保持 opaque。

### viewer `parseURL` 的索引边界

官方 `chuanyun-view` 的未剥离符号和反汇编进一步给出后缀索引的写入位置：

| URL 后缀索引 | viewer 成员偏移 | 观察到的处理 |
|---:|---:|---|
| `0` | `+0x130` | 保留为 `QString`，并参与日志/默认值处理 |
| `1` | `+0xc8` | 转为 `std::string`，日志文本标记为 host |
| `2` | `+0xe8` | 转为 `std::string` |
| `3` | `+0x108` | `QString::toInt(base 10)` |
| `4` | `+0x10c` | `QString::toInt(base 10)` |
| `5` | `+0x88` | 转为 `std::string` |
| `6` | `+0x68` | 转为 `std::string` |
| `7` | `+0x48` | 转为 `std::string` |
| `8` | `+0x28` | 转为 `std::string` |

这证明 `+` 后缀不是任意文本：至少一部分是数字端口/模式字段，且索引 1 才是
viewer 内部的 host 字段；命令前缀中的 `spice://127.0.0.1:10800` 是索引 0。当前
Linux parser 仍不对后缀做语义猜测，只保留有界字段，因为这些成员没有稳定的公开
跨版本名称和认证生命周期。

## `chuanyun-redirect.service` 的 root 策略前端（不纳入 Debian）

官方 UOS 服务文件把 `chuanyun-redirect` 作为 root 守护进程运行，失败自动重启，并把
官方 `bin/`、`lib/` 放入 `LD_LIBRARY_PATH`。对其字符串、导入和关联脚本的静态核对显示，
它除了调用公开 USB/IP 包装函数，还负责 `libudev` 监听、`99-usb-permissions.rules` 注入、
SysV `msgget/msgsnd/msgrcv` 队列、cJSON 设备状态表、deny/allow 策略和状态回报。这些是
厂商控制/监控面，不是 `usbip-core` 在 VHCI 上承载的 URB 数据面。

因此 Debian 适配明确不安装或启动该 service，不复制 udev 权限写入、root 自动重启、
SysV 策略队列、设备状态上报和 deny-list。生产路径只保留标准 USB/IP 控制面与显式的
busid 选择；实验性官方库 loader 的状态/日志回调为 `NULL`，不会把这些策略面偷偷带入
默认服务。该取舍保留 HID/storage 转发协议，同时避免把安全、监控和遥测行为当作驱动
依赖。
