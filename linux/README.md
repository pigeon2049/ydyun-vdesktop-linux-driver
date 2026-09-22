# YDYUN Linux USB 转发适配器

这是针对 Debian trixie x86_64 的 Linux USB 适配器。它不加载或转换 Windows `.sys` 文件，而是使用 Debian 内核的 `usbip-core`、`vhci-hcd`、`usbip-host`、`usb-storage/UAS` 和 `usbhid`。

## 当前能力

- 标准 USB/IP 远端设备列表、attach、detach、VHCI 状态查看。
- 显式配置的本地 USB 设备可通过 `usbip-host` export 给远端 USB/IP peer；本地导出默认关闭。
- 可配置设备的自动 attach/断线重试。
- 显式开启时，可对已验证的 ZTE 3246 USB 控制前缀做透明反向链路桥接；未知命令会被拒绝。
- 反向桥可显式选择官方静态证据对应的 9 字节 type/length/LZ4 封装；默认 `off`，不会自动猜测。
- USB 存储、鼠标、键盘由 Linux 原生 USB 类驱动接管。
- 配置和服务不包含 `UsbIpcGuard`、QoE/trace、进程监控、WinDivert、SysGuard 等组件。
- 另含 `ydyun-ice-probe`：只读离线 SPICE/ICE framing，不连接云端、不解密 TLS/KCP。
- 另含 `ydyun-session-probe`：校验已由官方客户端取得并解码的会话 JSON，只输出脱敏
  的 VMC/CAG/SCG 路由和密钥存在性，不登录、不解密、不连接云端。
- 另含手动启动的 `ydyun-chuanyun-session`：按官方 UOS 头文件的公开 ABI 加载操作者
  自行提供的 `libchuanyun.so`；默认不启动、不携带官方闭源库、不传入日志/网络质量/监控回调。
- 另含手动启动的 `ydyun-chuanyun-usb`：按官方 `libchuanyun_usbip.so` 的公开导出做
  列表、映射和解除映射；不启动 `chuanyun-redirect`，不执行 deny-list、udev 注入、SysV
  队列或状态监控。它只在显式 `run` 时加载操作者提供的厂商库。

## 安装依赖

```sh
sudo apt install usbip
sudo modprobe usbip-core vhci-hcd
```

Debian trixie 的 `usbip` 包已经提供 `/usr/sbin/usbip`；内核模块应来自正在运行的内核包。若 `modprobe` 报模块不存在，需要先安装与当前内核匹配的内核模块包。

## 配置

编辑 `/etc/ydyun-usb/ydyun-usb.conf`：

```ini
[connection]
remote_host = 192.0.2.10
remote_port = 3240

[devices]
1-2 = USB storage
1-3 = keyboard
1-4 = mouse

# 上行转发（可选，只有明确列出的本地设备会被 bind）
[local_devices]
# 3-9 = local mouse
# 3-10 = local keyboard
```

先查看远端导出的 bus ID：

```sh
sudo ydyun-usbctl list
```

也可以先只探测标准 USB/IP 控制面，不会 attach 任何设备：

```sh
sudo ydyun-usbctl probe
```

官方 Linux 客户端在已经由 JWAE/SCG 建好的会话链路上，会先发送额外的 USB/IP
`0x8008` link-init，收到 `0x0008` 后等待云端发送 `0x8005` DEVLIST 请求，再回送
`0x0005` 和设备数量。如果后续取得了合法、已建立的该类 endpoint，可用下面的只读命令
验证这个方向；它不会创建 JWAE/SCG 会话，也不会发送认证材料：

```sh
sudo ydyun-usbctl probe-link
```

安装后可以先运行只读部署自检；它不会连接远端、attach、unbind 或 export 设备：

```sh
sudo ydyun-usbctl doctor
```

再做一次性 attach：

```sh
sudo ydyun-usbctl attach 1-2
sudo ydyun-usbctl port
```

本机 USB 上行导出使用单独命令，不会自动接管物理设备：

```sh
sudo ydyun-usbctl local-list
sudo ydyun-usbctl export 3-9
sudo ydyun-usbctl unexport 3-9
```

`3-9` 只是示例，必须替换为 `local-list` 中确认的 bus ID，并先写入
`[local_devices]`。也可以显式启动 `ydyun-usb-export.service` 持续导出配置设备；该单元默认禁用。

安装包自带的 root-only vUDC 集成测试会创建一个临时的复合键盘、鼠标和存储设备，
通过 `usbipd` 导出后再导入本机。默认测试原生 USB/IP；安装完成后可用下面的命令让
最后一次 attach 经过已安装的 `ydyun-usbctl`，验证实际 Debian 包的控制面：

```sh
sudo YDYUN_E2E_CONTROLLER=1 make -C /opt/code/ydyun/driver/linux e2e-vudc-usb
```

要在不接管物理 USB 的前提下验证“本地 USB → usbip-host → usbipd → vhci-hcd”上行导出链路，
可使用内核 `dummy_hcd` 虚拟主机控制器：

```sh
sudo make -C /opt/code/ydyun/driver/linux e2e-dummy-host
```

该模式使用临时 ConfigFS HID+存储 gadget，普通 `usbipd` 服务端和安装版
`ydyun-usbctl export/unexport`；它不会解绑物理键盘、鼠标、摄像头或蓝牙。当前内核的
`dummy_hcd` gadget-side `hidg` 写入在没有待处理 interrupt-IN URB 时可能阻塞，因此测试对
HID 报告注入设置 2 秒上限；HID 描述符/类驱动枚举和存储读写仍然必须通过。

测试文件和临时配置都放在项目 `build/` 下，退出时清理；不会使用 `/tmp`，也不会连接
云端或接管真实 USB 设备。

导出会临时解除该设备接口上的 `usb-storage`、`usbhid` 等本地类驱动，解除导出后
重新触发内核探测。导出实体键盘或鼠标期间，本机对应输入会暂时不可用；不要把系统
根 Hub、蓝牙控制器或当前唯一输入设备加入配置，除非这是明确的云桌面转发目标。
控制器还会在 sysfs 中拒绝 USB hub/root hub 类设备，避免误解绑总线级设备。

如果现场确认云端使用原 Windows `usbipc_backward_link_server`，可在配置的
`[backward]` 中将 `enabled` 改为 `yes`；确认控制帧和标准 USB/IP 时序后，再将
`auto_attach` 改为 `yes`（设备导入超时由 `attach_timeout` 控制，默认 30 秒），并单独启动桥接单元：

```sh
sudo systemctl enable --now ydyun-usbctl-backward.service
# /etc/ydyun-usb/ydyun-usb.conf: [backward] auto_attach = yes
# Add the allowed bus IDs to the installed bridge config, then use the normal
# configured-busid command:
sudo ydyun-usbctl --config /etc/ydyun-usb/ydyun-usb-bridge.conf attach 1-2
# For automatic retry of storage/keyboard/mouse, use instead:
# sudo ydyun-usbctl --config /etc/ydyun-usb/ydyun-usb-bridge.conf watch
```

桥接器只接受已确认的 0x0202 版本标记、0x210 字节初始帧和 USB
`op_cmd` 1/2，然后把后续标准 USB/IP 字节转发到本机回环端口 3247；它会校验并
记录控制帧 `+0x190` 处的 bus ID。将 `[backward] auto_attach = yes` 后，op 1 会用
该 bus ID 调用原生 `usbip attach`，op 2 会查找匹配的本地 VHCI 端口并 detach；默认
仍为 `no`，因为现场必须先确认控制帧与标准 USB/IP 数据阶段的时序。它不解析或执行
打印机、扫描仪、摄像头、监控或安全命令。未启用自动导入时，仍可用桥接配置手工
执行 `attach` 或 `watch`。

服务运行：

```sh
sudo systemctl enable --now ydyun-usbctl.service
```

## 重要限制

当前实现已加入一个显式开启的 3246 控制前缀桥，但仍未宣称完成厂商私有协议适配：真实云桌面会话还需确认扩展字段、压缩、设备 bus ID、热插拔和断线时序。未知私有消息不会被自动执行。

本轮真实官方会话进一步确认：viewer 的本地 USB IPC 使用 16 字节小端命令头，官方
`usbredirect` 的本地入口通常在 loopback `3240`，viewer 会探测 `3240`–`3250` 范围；
云端 USB endpoint 前还有固定 0x74 字节的厂商代理前置，随后才进入标准 USB/IP
`DEVLIST/IMPORT/URB`。这些前置字段来自登录后的 JWAE/SCG/CAG 会话，不能把裸端口、
静态样本或本地探测结果当成授权连接；因此当前生产包仍不猜测实现它们。

官方麒麟 `libusbipd.so` 的静态反汇编显示，压缩分支使用网络顺序的
`type:u8 + uncompressed_size:u32 + compressed_size:u32 + payload` 封装：`type=0`
是原文，`type=1` 是 raw LZ4 block。桥接器在 `[backward]` 下提供
`compression = off|lz4`：`off` 逐字节转发；`lz4` 将本地标准 USB/IP 数据切成
有长度边界的 type-0 帧，并把远端 type-0/type-1 帧还原成标准 USB/IP 数据。这个选项
只代表封装转换，不代表认证、TLS 或云端会话已完成；真实会话确认前必须保持 `off`。

画面方面，`ydyun-ice-probe` 只用于分析已经取得的字节样本：

```sh
ydyun-ice-probe --link-header sample.bin
```

它不是 ICE 登录器、TLS/KCP 客户端或屏幕显示器；真实画面接收仍需完成隧道认证和
厂商扩展验证。对已经解出的标准 SPICE display 字节，可以将编码样本保存到当前目录
下指定的目录，随后交给 FFmpeg 检查编码器和解码能力：

```sh
ydyun-ice-probe --extract-dir doc/samples/display sample.bin
ffprobe doc/samples/display/stream-*-h264-*.es
```

提取器只写 `STREAM_DATA`/`STREAM_DATA_SIZED` 的长度受限样本，不会解密、连接云端或猜测
ZTE 私有帧头；输出目录由操作者显式指定，不使用 `/tmp`。

本机还可以运行离线画面数据面回归：它生成一帧测试 H.264，将其封装为标准 SPICE
display stream，经过已安装的探针提取，再交给 `ffprobe/ffmpeg` 验证并解码。该测试不
连接云端，只验证“标准 display stream -> 有界样本提取 -> Linux 解码器”的边界：

```sh
make -C /opt/code/ydyun/driver/linux e2e-spice-display
```

包内另有可选的 `ydyun-spice-viewer`，使用 Debian `spice-gtk` 创建标准 SPICE 窗口并接收
display channel；GTK widget 同时处理标准桌面键盘/鼠标输入。它支持普通 `HOST PORT`
和官方 viewer 边界的 `spice://HOST:PORT+opaque...` 形式：

```sh
ydyun-spice-viewer HOST PORT [TLS_PORT]
ydyun-spice-viewer 'spice://127.0.0.1:10800+opaque-session-fields'
```

它只连接操作者有权访问的标准 SPICE endpoint，不登录、不绕过 JWAE/SCG、不加载厂商
安全/监控库，也不打印 URL 后缀。中国移动云端的 vendor 隧道仍需合法会话先产生可访问
的本地端口；该命令是实际 Linux 显示/桌面键鼠入口，不等于已完成厂商私有建链。

如果通过合法的官方客户端调试流程取得了已解码的 `getFirmAuth` 响应，可以离线检查
路由字段；输入文件不会被原样回显，密码、token 和 auth code 只显示是否存在：

```sh
ydyun-session-probe decoded-firm-auth.json
```

如需把已经解码的、经授权取得的响应直接交给可选 public SDK loader，可让工具创建一次性
`0600` 配置文件；敏感字段只写入该文件，不打印到终端，也不会自动运行连接：

```sh
ydyun-session-probe decoded-firm-auth.json \
  --emit-loader-config ./authorized-session.conf \
  --library /path/to/official/libchuanyun.so \
  --server-ip 203.0.113.10 --server-port 443 \
  --terminal-sn terminal-id --unit-type debian
ydyun-chuanyun-session ./authorized-session.conf
```

该桥接只映射 `vmId`、`vmUserName`、`authCode`、`bizCode` 和显式 SDK 控制服务器字段；
它不从脱敏输出恢复秘密，不解密 `data`，不登录，也不启动 USB 或监控服务。输出文件已
存在时会拒绝覆盖。

在获得合法的官方 Linux SDK 和会话字段后，可用可选 ABI loader 做真实连接边界验证。
它不会从 `ydyun-session-probe` 的脱敏输出恢复认证材料；配置文件必须由操作者按授权
流程填写，且不要提交到仓库：

```ini
library = /path/to/official/libchuanyun.so
server_ip = 203.0.113.10
server_port = 443
vm_id = authorized-vm-id
username = authorized-user
auth_code = supplied-by-authorized-session
biz_code = supplied-by-authorized-session
```

运行：

```sh
ydyun-chuanyun-session ./authorized-session.conf
```

该工具只输出连接状态和首帧状态；不会输出 auth code、token、厂商错误字符串或网络
质量指标。真正的 JWAE/SCG、vendor SPICE/ICE 和画面窗口仍由外部官方库负责；USB
 转发服务不会因为执行该工具而自动启动或自动接管本机设备。

### 可选官方 USB ABI loader

官方 UOS/Kylin `libchuanyun_usbip.so` 还导出设备列表和按 busid 的 attach/detach。为
避免复制官方 root 策略前端，适配包提供一个交互式、默认不启动的兼容入口：

```ini
library = /path/to/official/libchuanyun_usbip.so
# 可选：官方 USB 服务需要的、由授权流程取得的 JSON；不会被工具回显
json_file = /path/to/authorized-usb.json
jwae_enabled = no
```

显式运行后，通过标准输入输入 `list`、`attach BUSID`、`detach BUSID`、`stop`：

```sh
sudo ydyun-chuanyun-usb ./authorized-usb-loader.conf run
```

如果现场确认需要由该库启动 JWAE，可在配置中显式设置 `jwae_enabled = yes`，并提供
授权 token、服务地址和端口；token 不会输出。该入口不安装 systemd 单元，不启动
`chuanyun-redirect`，不创建 SysV 队列，不读取或上报官方 `deny` 字段。真实云端行为
仍需用授权会话验证，不能把 loader 的 ABI 回归当成云端互通证明。

### 本机 USB/IP storage + HID 闭环测试

当前 Debian 内核如果提供 `usbip-vudc`，可以在物理机上执行 root-only 的复合设备回归：

```sh
sudo make -C /opt/code/ydyun/driver/linux e2e-vudc-usb
```

测试在 ConfigFS 创建一枚 8 MiB 虚拟 Mass Storage + HID keyboard/mouse 设备，启动
`usbipd --device`，再用本机 `vhci-hcd` attach。它要求 `lsusb -t` 同时出现
`Driver=usbhid` 和 `Driver=usb-storage`，并向 keyboard/mouse gadget endpoint 各发送一次
报告；随后解绑并清理 ConfigFS、usbipd 和测试盘。不连接中国移动云端、不挂载测试盘，
也不接管真实键盘鼠标。脚本位于
`linux/tests/e2e_vudc_hid.sh`，不是默认 systemd 服务或生产包内容。

还可以运行本地 vendor-control bridge 回归。它用一个测试云端进程发送已确认的
`0x0202/command=1/busid` 控制帧，再把 USB/IP 字节转发到真实本机 `usbipd`，用于验证
3246 桥接的配对和数据面；测试不包含认证、不访问云端、不接管物理 USB：

```sh
sudo make -C /opt/code/ydyun/driver/linux e2e-vudc-backward
```
