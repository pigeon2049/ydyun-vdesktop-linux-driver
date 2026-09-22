# YDYUN Linux 适配进度

更新时间：2026-09-22（Asia/Shanghai）

## 目标

将 `/opt/code/ydyun/driver/` 中的 Windows 云桌面 USB/输入转发能力，适配到 Debian trixie x86_64；第一阶段优先保证 USB 存储、鼠标和键盘转发，暂不移植安全、监控、遥测、打印机、扫描仪、摄像头、媒体和虚拟串口等非核心功能。

## 总体状态

当前阶段：**阶段 5/6：USB/IP 适配包稳定化与真实云会话交叉验证**

完成度：**约 98%**

当前可交付物：`../ydyun-usbctl_0.2.48-1_amd64.deb`（Debian trixie amd64，SHA-256
`9e5dd1ff3c1bb3c2aaac773f4d7856150d6a52aacd75e95d19950f3bc2ab9405`）。上一版 0.2.47-1 SHA-256 为
`00bfe275b2e15469a3833c0f0db0b5252e40aae1dc101618c020b9de495ee6bd`。
本地 USB/IP
数据面、HID/存储隔离端到端测试和标准 SPICE viewer 已完成；本轮还用官方 Linux 客户端
真实登录并取得了云端 H.264/SPICE 首帧、光标和键盘输入证据。清洁包尚未携带厂商
JWAE/SCG 私有建链或安全/QoE/监控组件，因此仍不宣称清洁包本身已经完成官方云端直连。

### 已完成

- [x] 确认原始目录是 Windows 安装包集合，不是可直接编译的源码工程。
- [x] 只读扫描原始文件，确认原始目录没有 `.c/.h/.cpp/.sln/.vcxproj`，也没有直接位于根目录的 `.sys/.inf`。
- [x] 将相关 NSIS/7z 嵌套安装包解压到 `doc/extracted/`，没有执行其中的 PE 文件。
- [x] 定位 USB 核心包 `guestos-usb.7z`。
- [x] 定位 USB/IP/VHCI/虚拟总线驱动和 `UsbIpc.exe`。
- [x] 通过 PE/驱动字符串确认其 USB 数据面使用 USB/IP 风格头部、URB、seqnum、bulk/interrupt/control/isochronous 传输。
- [x] 识别安全/监控/遥测/网络拦截组件，并决定首版不纳入 Linux 适配。
- [x] 确认 Debian trixie x86_64 内核版本为 `6.12.107+deb13-amd64`。
- [x] 新增 `linux/src/ydyun_usbctl.py`：配置校验、远端列表、attach/detach、VHCI 状态和重连。
- [x] 新增 systemd 服务、配置样例和 Debian amd64 打包骨架。
- [x] 生成 `build/ydyun-usbctl_0.1.0-1_amd64.deb`。
- [x] 通过 Python 编译检查和 4 个单元测试。
- [x] 确认本机内核提供 `usbip-core.ko.xz` 和 `vhci-hcd.ko.xz`。
- [x] 下载并解包 Debian trixie 官方 `usbip` 工具到 `doc/toolchain/` 做 CLI 兼容性检查。
- [x] 新增 `ydyun-usbctl probe`，以标准 `OP_REQ_DEVLIST` 安全探测 3240/3246 候选端口。
- [x] 对照公开 USB/IP 实现确认 `op_common/DEVLIST/IMPORT` 标准结构；参考源码和许可证保存在 `doc/reference/usbip-win/`。
- [x] 清理临时 `/tmp/ydyun-driver-extract.*` 副本，解压资料只保留在当前目录 `doc/extracted/`。
- [x] 最终回归：16 个单元测试通过，并新增本机 TCP 端到端桥接回归；当前 `.deb` SHA-256 记录在本节构建记录中。
- [x] 进一步反汇编确认 ZTE 私有统一控制面使用独立的固定上限消息和 `op_cmd` 分发器；未将未知私有报文猜测性实现进 Linux。
- [x] 进一步确认 3246 是 `usbipc_backward_link_server`：先收 0x210 字节控制帧，部分 USB 分支随后复用同一连接进入标准 `DEVLIST/IMPORT`；已记录扩展字段尚未完全还原。
- [x] 新增显式关闭的 3246 反向链路桥：只接受已确认版本/长度/USB 命令，并将后续标准 USB/IP 流转发到本机回环端口；未实现未知私有命令。
- [x] 还原 3246 op 1/2 控制帧中的 `+0x190` bus ID 字段，并加入显式开关控制的自动 attach/detach。
- [x] 按标准 `OP_REQ_IMPORT` 的 32 字节 bus ID 进行本地连接配对，避免多设备并发时仅按 FIFO 错配。
- [x] systemd 单元增加 `usbip-core`/`vhci-hcd` 的启动前加载，反向桥不再被模块沙箱配置阻断。
- [x] 修正 Debian 依赖：移除 trixie 中不存在的 `linux-tools-generic` 推荐项，直接依赖发行版 `usbip`。
- [x] 在当前 Debian 主机安装 `.deb`，加载 `usbip-core`/`vhci-hcd`，确认 VHCI 设备节点出现，并通过 `systemd-analyze verify`。
- [x] 静态确认 `support_compress_flag` 默认值、`compress_flag_threshold=0x2800` 和压缩分支的长度门槛；未猜测实现未知压缩包头。
- [x] 解包并静态分析 `VDesktop-setup-iceV7.25.43SP1.exe`：确认整屏画面不是 USB/IP，而是 ICE display channel + 专有隧道。
- [x] 确认 ICE 画面捕获包含 DXGI/D3D11/IDD 共享 surface、GPU H.264/HEVC 编码、ICE x264 风格编码器、dirty rectangle/cache 和 zlib/zstd 无损区域。
- [x] 确认 ICE 传输包含 TLS、TCP/UDP/KCP、多路径/拥塞控制，以及独立的输入和光标通道。
- [x] 确认当前物理 Debian 主机不能安装 Windows ICE display/input guest 驱动；已将画面适配与 USB Debian 包分开。
- [x] 进一步还原 ICE 的 display channel 消息名、surface/stream 创建层次、IDR 请求、dirty rectangle/frame cache 和无损区域边界，新增 `doc/ICE-PROTOCOL.md`。
- [x] 只读解包并分析 `uSmartPlayer`：确认它是 FFmpeg/mpv 媒体重定向播放器，补充了 TCP/UUID/媒体消息线索，同时确认不能把它误当作整屏 ICE viewer；其 `DriverController.dll` 安全/进程控制部分排除。
- [x] 通过 ICE 二进制反汇编确认 `0x51444552`/`REDQ` SPICE magic、标准通道枚举和 `spice_header.size` 处理；新增离线 SPICE framing 解析器与 `ydyun-ice-probe`。
- [x] 对照 Debian SPICE 0.15.2 `spice.proto` 补齐标准 `STREAM_CREATE`、`STREAM_DATA`、`STREAM_DATA_SIZED` 的元数据边界解析；探针可输出编码类型、stream ID、尺寸和编码样本长度。
- [x] 使用完全隔离的 `dummy_hcd + g_mass_storage + usbipd` 完成标准 USB/IP 端到端验证：已安装控制器成功 `probe/list/attach`，VHCI 导入由 `usb-storage` 接管，虚拟磁盘首扇区与远端 backing file SHA-256 一致；测试状态已全部清理。
- [x] 使用完全隔离的 configfs HID gadget 完成键盘/鼠标端到端验证：复合 HID 设备经 `usbipd → ydyun-usbctl → vhci-hcd` 导入后由 `usbhid` 创建 evdev，虚拟键盘扫描码和鼠标按键/移动报告均成功到达 `/dev/input/event*`；测试状态已全部清理。
- [x] 用 `dummy_hcd + g_mass_storage` 验证 Linux `usb-storage`/SCSI 类驱动链路；另用 `usbip-vudc` 验证 IMPORT 控制面可达但数据面尚未闭环，且已清理全部虚拟测试状态。
- [x] 从中国移动云电脑官方入口下载并静态分析 Windows、UOS AMD64、麒麟 AMD64 客户端；包、哈希、解包结果保存在 `doc/client-packages/`。
- [x] 反向确认官方 Linux 客户端采用 Electron + 原生 VDI/Spice SDK + 用户态 USB/IP/libusb/udev 路径，未将 Windows `.sys` 作为 Linux USB 核心。
- [x] 反向确认画面通过 `libspice-client-glib*` 的 display surface/stream 和视频解码链路转发，键鼠输入通过 Spice input channel；新增 `doc/CLIENT-ANALYSIS.md`。
- [x] 识别官方包中的 QoE、进程守护、日志/性能采集、probe、trace、安全依赖和维护脚本；确认最终 Debian 包不复制、不启用这些组件。
- [x] 增加显式配置的本地 USB 上行导出：自动解除选定接口上的本地类驱动，调用 `usbip-host` 导出，解除导出后触发内核重新探测；未列入配置的物理设备不会被接管。
- [x] 用 `dummy_hcd + g_mass_storage` 完成上行导出闭环：`ydyun-usbctl -> usbip-host -> usbipd -> usbip attach -> vhci-hcd`，并验证退出后测试 gadget、模块和 VHCI 状态均清理。
- [x] 构建并安装 `ydyun-usbctl 0.2.8-1 amd64`，新增的 `ydyun-usb-export.service` 保持 disabled/inactive。
- [x] 新增 `ydyun-session-probe`：对已解码的官方 `getFirmAuth` JSON 做路由校验和脱敏输出，绝不登录、解密或连接云端。
- [x] 扩展 `ydyun-ice-probe --extract-dir`，按标准 SPICE display stream 长度字段提取编码样本，输出目录由操作者显式指定。
- [x] 修复 3246 反向桥的异步控制帧/本地 IMPORT 启动竞态；本地 import 在有限超时内等待匹配 bus ID，网络集成测试连续 20 次通过。
- [x] 在 Debian trixie 安装发行版 `spice-gtk` 开发栈，并用 `-Wall -Wextra -Werror` 编译独立的标准 SPICE GTK viewer harness；未将其或官方闭源库放入 USB 驱动运行时依赖。
- [x] 为本地 USB export 增加 sysfs USB hub/root hub 拒绝保护；普通 storage/HID 仍可显式导出，32 项回归测试通过。
- [x] 根据官方 `zteWorker.connect` 参数补齐 `cagIpv6` 会话字段和 IPv6 CAG 路由计数。
- [x] 静态确认 `libjwae` 没有可复用头文件/调试 ABI，明确禁止猜测 `ctypes`/C 调用进入生产包。
- [x] 新增只读 `ydyun-usbctl doctor` 部署自检；不会连接远端或改变任何 USB/VHCI 状态。
- [x] 从官方麒麟 `libspice-client-glib` 的 DWARF 调试信息静态还原 `SpiceConnection` 字段布局和连接入口原型，结果记录到 `doc/CLIENT-ABI.md`；未执行或复制闭源库。
- [x] 进一步从 `chuanyun-view` DWARF 还原 `ConnectionConfig`/`CommonConfig` 字段，区分 USB/传输核心与 SDK 采集、公网探测、外围功能字段。
- [x] 从官方麒麟 `libusbipd.so` 静态还原 9 字节 type/长度/LZ4 USB 封装；反向桥增加默认关闭的 `compression=off|lz4` 转换和边界校验。

### 进行中

- [x] 用隔离的 dummy_hcd 虚拟 Mass Storage 真实验证官方 viewer 的 USB 导入：代理前置、
  标准 DEVLIST/IMPORT、stub_rx/stub_tx 和 URB 链路均出现，未触碰实体 USB。
- [ ] 将合法会话控制面提供的私有 import payload 接入清洁 Linux 数据面；不猜测认证字段。
- [ ] 继续确认 5100/19000 的实际用途；已知 5100 默认 `LISTEN_OPEN=0`。
- [ ] 用真实云桌面会话抓取 ZTE 控制面消息并确认标准 USB/IP 端口/压缩方式。
- [ ] 验证 ZTE 可选 `compress_flag`/`deal_compress_submit` 是否对 Linux 客户端生效。
- [ ] 取得真实压缩/未压缩 USB 会话，确认包头、算法、原始长度、压缩长度和序号语义。
- [ ] 取得真实 ICE 云会话，确认显示通道握手、TLS/KCP 参数、帧头、H.264/HEVC/无损区域封装和多屏合成顺序。
- [ ] 单独设计 `ydyun-ice` Linux 接收原型；不把画面协议耦合进 `ydyun-usbctl`。
- [x] 已实现标准 SPICE header 与 display stream 元数据离线剥离器；[ ] 真实 ICE 会话的 TLS/KCP 解出字节和厂商扩展仍待样本验证。

### 待完成

- [x] 在 root 环境安装/确认 `usbip` 工具及 `vhci-hcd` 模块可用性。
- [x] 编写 Linux 控制器和配置文件。
- [x] 用本地模拟服务完成标准 DEVLIST 握手测试；真实云端互通仍待现场会话。
- [ ] 若云桌面端不是标准 USB/IP，补充 ZTE 控制面兼容层。
- [ ] 使用真实云桌面会话测试 USB 存储、键盘、鼠标的插拔、传输、重连和卸载。
- [x] 构建 Debian trixie x86_64 `.deb`，完成包内容、架构和 Depends 元数据验证。
- [x] 完成本机 root 安装和空配置安全验证。
- [x] 最终包审计确认仅依赖 `python3`/`usbip`，不含安全、监控、遥测或网络拦截二进制。
- [ ] 在真实云会话中完成 USB 存储、键盘、鼠标的导入、插拔、传输、重连和卸载测试。
- [ ] 获取真实中国移动云桌面会话，验证官方反向分析得到的 3246/ZTE USB 控制面、Spice display/input 会话和认证参数。

### 本轮构建记录

- 16 个 Python 单元测试通过，其中包含本机模拟云端的 3246 控制帧、标准 IMPORT、bus ID 配对和双向透明转发测试。
- 历史 `build/ydyun-usbctl_0.1.0-1_amd64.deb` SHA-256：`2a7debe502019416f3a52bde0157dfecb20cfa480ba34fab8f38e49b7becbb7b`。
- 标准 `dpkg-buildpackage -us -uc -b` 构建也已通过，产物位于 `/opt/code/ydyun/ydyun-usbctl_0.1.0-1_amd64.deb`；最终包 Depends 仅为 `python3, usbip`。
- 上一版最终构建运行 22 项测试全部通过；本轮新增 display stream 解析测试后共 24 项测试，包内容未发现 `.exe/.dll/.sys/.pdb` 或安全、监控、遥测、网络拦截相关文件。
- 最终包已安装到本机；两个 systemd 单元均保持 `disabled/inactive`，`usbip port` 为空，物理 USB 键盘、鼠标、摄像头和蓝牙设备仍正常枚举。
- 包重新安装后重新加载 `usbip-core`/`vhci-hcd`，`usbip port` 正常显示空的 VHCI 导入列表。
- 虚拟 USB 证据：`dummy_hcd + g_mass_storage` 触发 `usb-storage` 和 `scsi host2`；`usbip-vudc` 实验的 `OP_REQ_IMPORT` 到达 `usbipd`，但数据通道以 `-32` 退出，不能替代真实云端验证。
- 新增标准 USB/IP 端到端证据：本机 `usbipd` 导出虚拟 busid `21-1`，控制器通过标准 `OP_REQ_DEVLIST/OP_REQ_IMPORT` 导入到 VHCI `Port 00`，Linux 创建 `/dev/sda`，读取首扇区与 backing file 的 SHA-256 均为 `ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7`；随后已 detach 并删除虚拟测试镜像。
- 新增 HID 端到端证据：虚拟键盘产生 `EV_KEY` 扫描码，虚拟鼠标产生 `BTN_LEFT` 与 `REL_X` 事件，证明 Linux 原生 `usbhid/evdev` 数据面可承接 USB/IP 导入；随后已 detach、卸载 `usb_f_hid/libcomposite/dummy_hcd/usbip-host` 并删除测试 gadget。
- 新增 8 个 SPICE framing/display 测试，当前总测试数为 24；`ydyun-ice-probe` 的输出代表标准 SPICE header 和 display stream 元数据解析，不代表 ICE 登录或画面显示已完成。
- 当前最终包 `build/ydyun-usbctl_0.2.10-1_amd64.deb` SHA-256：`a04c902950ef7945516fd522c13d6a01469142afded92e4cf906f11cc8ca1b5c`；34 个单元测试、严格 warning 检查、Python 编译检查和 systemd 单元校验均通过。
- 新版包新增 `ydyun-usb-export.service`、`[local_devices]` 和 `export/unexport/export-watch`，默认仍 disabled；包内容审计未发现 `.exe/.dll/.sys/.pdb`、QoE、监控、遥测、网络拦截或安全代理文件。
- 上行虚拟闭环证据：文件型存储 gadget `21-1` 自动解绑 `usb-storage` 后由 `usbip-host` 导出，`usbipd:3248` 返回 DEVLIST，`usbip attach` 导入 VHCI Port 00；清理后物理设备和 VHCI 状态未改变。
- 官方 SDK 静态交叉验证完成：Linux `connectVm`、USB JWAE 服务、Spice display/input 符号和 Windows `RemoteDesktopSDK.h` 的 USB/画面分界已记录；未执行官方二进制，也未把闭源安全、监控、QoE 或遥测组件带入 Debian 包。
- 进一步还原官方会话入口：`/terminal/cc/getFirmAuth/v1` 返回 VM/VMC/CAG/SCG 参数，`chuanyun-view` 通过 `libjwae`/SCG、`ConnectionConfig` JSON 和 session ID 建立厂商 SPICE；确认不能用裸 3240/3246 探测替代真实会话。
- Debian 包已安装 `ydyun-session-probe`，实测仅输出 VMC/CAG/SCG 路由和密钥存在性；样例密码、token、auth code 均未出现在输出中，缺少完整路由时返回退出码 2。
- `ydyun-ice-probe --extract-dir` 已用 H.264 样本完成离线提取回归；提取器不连接网络、不解密 TLS/KCP、不处理未验证的厂商扩展。
- `linux/experimental/ydyun_spice_viewer.c` 已在本机编译为 x86_64 ELF，确认公开 SPICE display/input 到 GTK 的下层接口可用；它仍不包含厂商 JWAE/SCG 会话。
- viewer harness 在 `xvfb-run` 下连接本机关闭端口时进入标准 SPICE channel、收到连接错误事件并以退出码 0 清理退出，断线生命周期已验证。
- 本轮增加官方 LZ4 USB 封装的 type-0/type-1 离线解析和反向桥转换测试；压缩选项默认关闭，未宣称真实云会话已验证。
- 已构建并安装 `ydyun-usbctl 0.2.18-1 amd64`；`doctor` 通过，三个 systemd 服务保持 disabled/inactive，`/usr/sbin/usbip port` 为空。
- 当前包 `build/ydyun-usbctl_0.2.18-1_amd64.deb` SHA-256：`e7e5250667bbee7036a7af366ffa08f8bc4101a7105f98229b18ef8af582ed19`；39 个单元测试、Python 编译、systemd 校验和包审计均通过，主链路、画面控制面、公开 Linux ABI、JWAE `StartConfig` 静态证据和 loader 使用文档已进入包内。
- 已将官方 UOS USB 前端拆分结论加入包文档并构建安装 `ydyun-usbctl 0.2.19-1 amd64`；新包 SHA-256：`51538df5ee6512b5cbe4e11d93a87e7509aea21334ca177dfc598cb696e8581a`。39 个单元测试、`make check`、systemd 校验和包审计通过；三个服务仍为 `disabled/inactive`，未创建 SysV 队列或 udev 策略规则。
- 已构建并安装 `ydyun-usbctl 0.2.20-1 amd64`；最终包 SHA-256：`e8eb1497796e10d2386a15870769968eaf2e3a06ff1536cf6330dcd61281bde9`。新增 `ydyun-chuanyun-usb` 公开 ABI loader 和 fake fixture 回归；39 个单元测试、`make check`、systemd 校验、doctor 和包审计均通过，三个服务仍为 `disabled/inactive`，VHCI 无导入设备。
- 新增静态调用图证据：官方压缩开关位于连接对象偏移 `+0xe30`，`send_xfer_data` 与 USB/IP DEVLIST/IMPORT 都经 `network_send_data`/`network_recv_data`，确认 LZ4 是外层字节流包装。
- 从官方 `chuanyun-view`/SPICE 符号确认画面 surface/cursor/脏区回调与 SPICE inputs 鼠标、键盘回调，完成画面转发与 USB HID 转发的分层记录。
- 完成官方 `libusbipd.so` 主链路静态调用图：JWAE/SCG `g_link` 上先执行 `0x8008/0x0008` 握手，再处理 `0x8005` DEVLIST 与 `0x8009` PRE_IMPORT；确认 `android_usbipd_start` 是本机 RemoteHub/libusb 导出服务，不是云端拨号入口。
- 真实官方云端 USB/画面会话仍待合法会话参数或抓包样本验证；当前不猜测 `libjwae` ABI，不把安全、监控、热插拔策略或闭源库复制进 Debian 包。
- 找到官方 UOS 头文件 `ccsdk/uos/include/chuanyun_api.h`，确认 `chuanyun_init/connectVm/disconnect/deInit` 及连接/首帧回调的准确 Linux C ABI；新增默认不启动、只在显式调用时加载外部厂商库的 `ydyun-chuanyun-session` 兼容入口。
- 从官方 UOS `chuanyun-view` 的 DWARF 和 `QSpiceConn::startSCG()` 还原 `StartConfig` 112 字节布局，确认 `jwae_set_token` -> `jwae_start` 是 vendor SPICE/ICE 与 USB/IP 主链路的共同会话前置；日志配置、网络质量和 cloud-peripheral 统计结构未纳入精简适配器。画面/GUI 输入与 USB storage/HID 的分流已写入 `doc/CLIENT-ABI.md`、`doc/CLIENT-ANALYSIS.md` 和 `doc/ANALYSIS.md` 第 40 步。

## Step 41：官方 UOS USB 前端拆分

- 已完成 `chuanyun-redirect`、`libchuanyun_usbip.so`、`libusbipd.so` 的静态拆分，结论单独记录在 `doc/USB-FRONTEND-ANALYSIS.md`：官方前端是 root + SysV 队列的本地策略服务，不是云端 USB 协议。
- 静态确认 SysV key `0x3b6`、请求/状态消息类型 100/101，以及命令 1/2/3/4/9 的启动、停止、按 busid 映射、解除映射、列表语义；deny-list、udev 注入和状态 JSON 属于安全/监控/策略路径，未复制到 Debian 包。
- 静态确认 `cdp_usbip_start_jwae_service`、token、RemoteHub 和 map/unmap API 仍依赖外部闭源会话，不能把本地队列或 3246 端口当作新版云端连接器。

## Step 42：增加官方 USB 公开 ABI loader

- 从同一官方库继续确认 `cdpusblib_get_device_list/free_device_list` 与 `cdpusblib_attach_device/unattach_device` 的公开 ABI、0x494 字节设备记录和字段偏移；新增显式 `ydyun-chuanyun-usb` loader。
- loader 只在显式 `run` 时加载操作者提供的厂商库，支持 `list`、`attach BUSID`、`detach BUSID`、`stop`，不启动 `chuanyun-redirect`，不读取/输出 deny 字段，不创建 SysV 队列、udev 规则或监控回调。
- 新增 fake vendor ABI fixture，`make -C linux check` 已覆盖动态加载、列表解析和 attach/detach 调用回归；真实云端 `g_link` 仍未被该离线 fixture 冒充验证。

## Step 43：补齐官方 USB 动态依赖证据

- 对官方 UOS ELF 的 `DT_NEEDED` 复核确认：`libchuanyun_usbip.so -> libusbipd.so + libudev.so.1 + libjwae.so`，`libusbipd.so -> libjwae.so`；其 RUNPATH 是构建机路径，不是可直接安装的 Debian 路径。
- 因此 `cdpusblib_*` 只能作为显式外部厂商运行时入口，不能被包装成脱离 JWAE/SCG 的通用 Linux 驱动；当前 Debian 包继续不携带、不伪造、不自动加载这些库。

## Step 44：从 Electron 源码复核画面与监控分层

- 静态读取官方 UOS `app.asar/src/main/index.js`，确认 `worker-connect` 只把 VM/VMC/CAG/SCG 参数交给 `runWorker` 或 `zteWorker` 原生 SDK；Electron 主进程不实现画面帧或 USB URB 数据面。
- 静态读取 `src/main/usb.js`，确认其只做本地 VID/PID、接口类和设备类型扫描；静态读取 `src/main/probe.js`，确认性能、RTT/丢包、Wi-Fi、外设和显示器信息采集/上报属于独立 QoE/监控路径，未纳入 Debian 适配。
- 画面结论进一步收敛为“JWAE/SCG 会话 -> vendor display/input -> 解码/窗口”，USB storage/HID 为同会话下独立 USB/IP forwarding；新增细节已写入 `doc/CLIENT-ANALYSIS.md` 第 19 节。

## Step 45：确认官方 Qt viewer 的显示/输入/USB 二进制边界

- 静态分析官方 `ccsdk/uos/bin/chuanyun-view` 的 `DT_NEEDED` 和未剥离符号，确认它直接依赖定制 `libspice-client-glib-2.0.so.8`、`libjwae.so`、Qt5 和 OpenSSL。
- 确认 `QSpiceConn::startSCG/StartConnection/openSession`、`QSpiceWidget::displayPrimaryCreate/displayInvalidate/displayOpenGlInvalidate`、`QSpiceHelper::inputs_modifiers/main_mouse_update` 与 `QSpiceConn::StartCdpUSB/InitCdpUSB/DealCdpUSB` 的分层边界。
- 画面 surface、桌面键鼠、USB redirect 是同一 viewer 内的三个独立功能面；生产 Debian 包继续不执行、不重新分发闭源 viewer，只保留标准 SPICE harness、USB/IP 控制器和显式外部 ABI loader。

## Step 46：增加公开会话 ABI 的离线回归

- 新增本地 fake `libchuanyun.so` fixture，`make -C linux check` 现在验证 session loader 的初始化、连接状态回调、首帧回调、退出断开和 `deInit` 清理。
- 该 fixture 不联网、不加载官方 ELF、不包含真实凭据；它只证明 loader 与已公开 `chuanyun_api.h` 的调用约定，不能替代真实 JWAE/SCG 或云端画面/USB 验证。

## Step 47：还原 Qt viewer 内部 USB 初始化边界

- 从 `chuanyun-view` 的 `QSpiceConn::StartConnection/InitCdpUSB/DealCdpUSB/UninitCdpUSB` 静态调用图确认：USB 初始化在 vendor SPICE 连接配置之前按条件启动，并通过 `DeviceRedirectManager::InitCdpUSB(string,string,int,string)` 接收 CommonConfig、外围配置和会话字段。
- 明确 `cdpusblib_get_device_list/attach/unattach` 不能单独替代 viewer 内部 manager 的私有配置和自动策略；当前 loader 保持最小公开 ABI，不猜测私有结构，真实云端联调仍需合法会话样本。

## Step 48：确认 viewer 私有 USB manager 不是可复用数据面 ABI

- 通过官方 `chuanyun-view` 的 DWARF 和函数体反汇编确认 `DeviceRedirectManager` 的私有 `InitCdpUSB/StartCdpUSB/UninitCdpUSB/StopCdpUSB` 签名，以及 `StartCdpUSB -> generateTargetJson -> MessageQueue::send` 的调用边界。
- 确认 `InitCdpUSB` 会读取 INI、解析 allow/deny 列表并启动 Qt 消息线程；viewer 自有 `UsbDeviceInfo` 为 420 字节，和公开 USB 库 0x494 字节设备记录不同。
- 因此没有把私有 manager 通过对象偏移、dlsym 或手写 C++ ABI 接入 Debian 包；生产实现继续只保留标准 USB/IP 和显式公开厂商 C ABI，安全/策略/监控线程不进入默认适配。
- 文档变更后已重新构建并安装 `ydyun-usbctl 0.2.20-1 amd64`；当前包 SHA-256：`daa8da3a7049d2ab945bd6462e9e07a1c2a834bb5cb357dc5216c244795ce620`。39 个单元测试、C ABI fixture、systemd 校验、`ydyun-usbctl doctor` 和包安装均通过。

## Step 50：补齐标准 SPICE 键鼠消息解析并复核 Electron USB 仅为状态层

- `linux/src/ydyun_spice.py` 新增 `decode_inputs_message()`，覆盖键盘按下/释放、扫描码、modifier、鼠标相对移动、绝对位置、按下/释放，并对每种消息执行长度边界校验。
- SPICE 回归测试从 39 项增加到 41 项，键鼠消息结构化解析和截断输入拒绝均通过；公开 SPICE harness 仍不连接厂商云端。
- 逐行复核 UOS `src/main/usb.js`/打包后的 `out/main/index.js`，确认 Electron 仅轮询本地 USB 状态并记录 `plug/unplug/link/unlink`，没有 USB/IP URB 或画面帧数据面；该状态/QoE 路径继续不进入 Debian 包。
- 已重新构建并安装 `ydyun-usbctl 0.2.20-1 amd64`；当前包 SHA-256：`4225d4ef3c4609be6bfb052b4cdf8faed6b7fd9aee2dd68c41338fc6fe1f8254`。41 个单元测试、systemd 校验、包审计和 doctor 均通过。

## Step 51：确认官方 viewer 不直接使用公开 USB C ABI

- 对官方 `chuanyun-view` 的 `DT_NEEDED`、动态导入和未剥离符号做交叉核对：viewer 导入
  的是定制 `libspice-client-glib-2.0.so.8` 的 SPICE USB manager 接口，而不是
  `libchuanyun_usbip.so` 的 `cdpusblib_*` 公开入口。
- 确认 viewer 直接使用 SysV `msgget/msgsnd/msgrcv/msgctl`、`libusb-1.0.so.0`，并保留
  `DeviceRedirectManager`、`MessageQueue`、`scanUsbDevicesWithLibusb` 等本地策略/扫描
  证据；这些属于自动重定向和控制面，不纳入 Debian 数据面。
- 更新 `doc/CLIENT-ABI.md` 与 `doc/USB-FRONTEND-ANALYSIS.md`，明确公开 C ABI loader
  不能替代官方 Qt viewer 私有 manager，默认实现继续优先标准 USB/IP + Linux 类驱动。
- 已重新构建并安装 `ydyun-usbctl 0.2.20-1 amd64`；当前包 SHA-256：`207c228b333af8d346e24c69dad84d991de0ebacb980e3d125acdcc942d7aab3`。
  41 个 Python 单元测试、session/USB fake C ABI fixture、systemd 校验、包审计和物理机
  `ydyun-usbctl doctor` 均通过；当前主机可见 `usbip-core`/`vhci-hcd`，无可附加的本地
  USB busid。

## Step 52：修正 SPICE 可变长度扫描码并增加键鼠上行编码

- 根据本地 SPICE 0.15.2 `spice.proto`、生成 marshaller 和服务端接收缓冲确认：
  `SPICE_MSGC_INPUTS_KEY_SCANCODE` 是可变长度 `Data`，不是固定 4 字节整数。
- `decode_inputs_message()` 现在返回受 2 KiB 限制的原始扫描码序列；新增
  `encode_inputs_message()`，覆盖 key down/up、scancode、modifier、相对/绝对鼠标和
  鼠标按键，严格校验字段范围后生成标准 `SpiceDataHeader` 或 `SpiceMiniDataHeader`。
- 新增键鼠编码、可变扫描码和边界拒绝测试；真实会话发送仍需 vendor/JWAE/SCG channel
  negotiation，未把未授权云端连接写入默认服务。
- 已重新构建并安装 `ydyun-usbctl 0.2.20-1 amd64`；当前包 SHA-256：`06019ecfb03413670ef163d5b1adee8121c75870e4aad4e254c9c910df794148`。
- 新增本机 TCP fixture，实测 `probe_protocol()` 的 USB/IP `0x0111 + OP_REQ_DEVLIST`
  请求和 `OP_REP_DEVLIST` 设备数响应；Python 单元测试现为 44 个。
- 上述协议测试不连接外网、不加载设备、不改变 VHCI 或 USB 绑定；真实云端 attach 仍需
  合法会话和远端设备端点。
- 在当前物理 Debian 主机用系统 `spice-client-gtk-3.0` 成功编译并启动
  `build/ydyun-spice-viewer`；对无服务端口的连接按预期产生标准 SPICE connect error
  并退出，证明 GTK/SPICE harness 的本机依赖和错误路径可用，但不作为云端画面互通证明。

## Step 49：把画面转发收敛到标准 SPICE display/input 消息

- 使用随本地参考源码保存的 SPICE 协议枚举和生成结构，确认 display channel 的 mode、surface、绘制/脏区、stream create/data/destroy 和 cursor 分工；视频帧是可变长 `data[]`，不是 USB/IP 数据。
- 确认 inputs channel 的键盘 `KEY_DOWN/UP/SCANCODE`、鼠标 motion/absolute position/button 消息以及 modifier 同步字段，绝对鼠标包含 `x/y/buttons_state/display_id`。
- 画面接收端的工程边界现固定为“JWAE/SCG 会话 -> vendor/custom SPICE -> display/input 解码 -> Linux 窗口”；USB storage/HID 仍是独立 USB/IP forwarding。当前只扩充文档，不将未验证的云端 codec/私有 channel 猜测写入生产包。

## 当前架构结论

原包的 Windows 内核路径大致是：

```text
远端/终端会话
    -> UsbIpc.exe（USB/IP 数据面 + ZTE 控制面）
    -> usb_vhci_client.sys / zteusbb.sys / USBIPEnum.sys
    -> Windows USB 栈
    -> USB Mass Storage / HID 等设备驱动
```

Linux 首选路径：

```text
远端/终端会话
    -> ydyun-usbctl（用户态控制面兼容层）
    -> Linux usbip-core + vhci-hcd
    -> Linux USB 栈
    -> usb-storage / usbhid
```

这样无需移植 Windows `.sys`，也不需要自制 HID 或块设备内核驱动。USB 存储、鼠标和键盘均由 Linux 已有的标准类驱动处理。

## 明确排除的组件

- `UsbIpcGuard.exe`：守护、拉起/终止 `UsbIpc.exe`、修复服务状态、枚举打印机并上报。
- `ZProcessMonitor.sys`、`ServiceChecker.exe`、`ZProCtrl.exe`：进程/服务监控。
- `vmusbtrace*.dll`、`qoelog*.dll`：QoE、trace、遥测。
- `WinDivert*.sys`、`WinDivert.dll`：Windows 网络拦截，主要服务网络打印机/扫描仪通道。
- `SysGuard.exe`、`GuardAgent.exe`、`SysMonitorBall.exe`、`forcloud_agentless_sensor*.exe`、`VMSecNotifier.exe`：系统安全/监控/传感器。
- `vdiskmrx*.sys`：Windows 网络重定向文件系统，不是 USB 存储设备转发核心。
- 摄像头、虚拟串口、虚拟扫描仪、媒体重定向和打印机工具：首版不纳入。

排除安全/监控组件不等于关闭 Linux 基础安全：最终服务仍会采用最小权限、显式允许的远端地址、长度/序号校验和 systemd sandboxing。

## 主要风险/未知项

1. 原包没有源码，无法直接确认厂商私有控制面所有消息结构。
2. `UsbIpc.exe` 的字符串显示标准 USB/IP 头部和 `usbip_xmit`，但也显示 ZTE 扩展控制命令、压缩和端口 3246/5100/19000；不能仅凭字符串保证 Linux 标准 `usbip` 工具可直接连接。
3. Windows 包同时存在旧的 `USBIPEnum`、旧 `zteusbb` 和新的 UDE `usb_vhci_client`，可能是按 Windows 版本选择不同后端。
4. 没有真实云端会话和抓包数据时，Linux 适配可以先完成标准 USB/IP 路径，但 ZTE 控制面仍需实机验证。

5. 私有统一控制面已确认是额外的 0x210 字节消息分发链路；已还原 bus ID 字段位置，并实现默认关闭、显式开启的自动 attach/detach。

6. 整屏画面转发不在 USB/IP 内：ICE 包的 `IceDisplay`/`IceVGPUCapture` 负责捕获与编码，`IceTunnel` 负责 TLS + TCP/UDP/KCP 传输，`ProtocolManager` 负责 ICE/RDS 会话和显示通道；媒体包里的 DirectShow 重定向只是播放器音视频加速。

7. 本机是物理 Debian 主机。ICE 的 `ice.sys`/`icedod.sys`/`iceidd.dll`/`ICEVGEmBus.sys` 是 Windows 云桌面 guest 侧驱动，不能直接安装到本机；Linux 侧画面实现应是协议接收器，USB 侧则继续使用标准 `usbip-core`/`vhci-hcd`。

## 下一步

1. 在合法云端会话中执行 `ydyun-usbctl probe/list`，确认远端是否直接提供标准 USB/IP
   `3240` 端点，并用真实 storage/HID 设备做 attach、热插拔和断线测试。
2. 若标准端口不通，使用已确认的 `3246/5100/19000` 控制面证据采集真实控制帧、压缩
   和 bus ID 时序，再扩展 ZTE 兼容桥；未知命令继续拒绝。
3. 取得合法、已解码的 vendor session 参数后，验证 `ydyun-chuanyun-session` 与官方
   `libchuanyun.so` 的连接/首帧回调，并把已授权会话接入画面/inputs harness。
4. 对真实 ICE/SPICE display 样本确认 codec、surface 绘制和 stream data，再实现 Linux
   解码/显示原型；不把猜测性 vendor 协议写入当前 `.deb`。

## Step 53：完成物理机 USB/IP HID + storage 内核闭环

- 当前 Debian trixie 内核提供 `usbip-vudc`、`usb_f_hid`、`usb_f_mass_storage`；新增
  root-only `linux/tests/e2e_vudc_hid.sh` 和 `make -C linux e2e-vudc-usb` 目标。
- 实测在 ConfigFS 创建复合 gadget，通过 `usbipd --device` 导出到本机 TCP 3249，再由
  `vhci_hcd` attach；`usbip port` 显示 `usbip-vudc.0`，`lsusb -v` 显示 2 个 interface：
  HID keyboard（中断端点）和 Mass Storage（SCSI/Bulk-Only 双 bulk 端点）。
- `lsusb -t` 的真实绑定结果为同一设备的 `Driver=usbhid` 与 `Driver=usb-storage`；
  `lsusb -v` 同时显示 HID keyboard protocol 1 与 HID mouse protocol 2，脚本向
  `/dev/hidg0`/`/dev/hidg1` 各写入一次报告，证明 Linux 原生类驱动可以承接键盘、鼠标
  和存储，而无需移植 Windows `.sys`。测试结束后已确认 VHCI port、usbipd 和 ConfigFS
  gadget 均清理。
- 进一步给 8 MiB backing file 建立 `YDYUN_E2E` ext4 label，实测客户端创建
  `/dev/sda`，通过 USB/IP 写入扇区 1000 后再读回并 `cmp` 校验成功；这证明了真实
  SCSI/Bulk-Only storage 数据读写，不只是枚举和驱动绑定。
- 初版测试暴露了 `/bin/sh`/dash 对 `printf %b \xNN` 的差异，已改为 Bash 并重新验证；
  修正后 HID report descriptor 长度为键盘 47 字节、鼠标 52 字节，内核日志不再出现
  report parser 的伪错误，两个 HID input reader 和两个 gadget report 写入均通过。
- 已重新构建并安装 `ydyun-usbctl 0.2.20-1 amd64`；当前包 SHA-256：`5ecb5f8115b62d5cf4a4a16cdb785ee7df1ecb9a1c491616667daeac48a2170a`。
  44 个 Python 单元测试、`make -C linux check`、root vUDC 复合 USB 闭环、systemd 校验、
  包审计和物理机 `doctor` 均通过；三个 systemd 单元保持 disabled/inactive。
- 该闭环验证的是 Linux USB/IP 内核数据路径，不等于中国移动 JWAE/SCG 云端会话已完成；
  云端剩余工作仍是把已授权厂商会话的控制面接到标准或 ZTE 兼容 USB/IP 端点。
- 参考上游 Linux 的 [vUDC server example](https://github.com/torvalds/linux/blob/master/tools/usb/usbip/vudc/vudc_server_example.sh)
  和 [usbip-vudc sysfs ABI](https://kernel.googlesource.com/pub/scm/linux/kernel/git/rzhang/linux/+/release/Documentation/ABI/testing/sysfs-platform-usbip-vudc)。

## Step 54：官方 Linux 客户端三条数据面与 JWAE 监控边界复核

- 对 UOS `chuanyun-view` 的导入符号、DWARF 文件名和运行时字符串做定向复核：
  `QSpiceConn::StartConnection/startSCG/openSession` 负责会话，
  `QSpiceWidget::displayPrimaryCreate/displayInvalidate/displayOpenGlInvalidate` 负责
  surface/脏区/OpenGL 更新，`QSpiceInputsChannel` 负责键鼠上行；这些都不是 USB/IP 字节流。
- 同一 viewer 还直接使用定制 `libspice-client-glib` 的 `spice_usb_device_manager_*`、
  本地 `libusb`、`DeviceRedirectManager` 和 SysV `MessageQueue`。这说明自动 USB 重定向/策略
  队列是 viewer 私有控制面，不能用公开 `libchuanyun_usbip.so` C ABI 或对象偏移替代；安全、
  allow/deny、udev 和策略线程继续排除。
- 对 `libusbipd.so` 的导出和本地符号再次确认：`handle_usbip_link_init`、
  `OP_REQ_DEVLIST/OP_PRE_REQ_IMPORT`、`network_send_data/network_recv_data` 与 LZ4 外层封装
  同处 USB 主链路；`android_usbipd_start` 仍是本机 RemoteHub/libusb 导出服务，不是云端拨号入口。
- 对 `libjwae.so` 只读符号/字符串确认其包含 `jwae_set_token/start/stop` 以及 DTLS、trunk、
  side-channel、redirect-manager、monitor-server 和网络质量结构；它是认证/隧道运行时，不是
  可安全复制的普通画面库。生产 Debian 包不携带、不执行、不猜测其 ABI，也不移植
  `monitor-agent`/QoE/trace 路径。
- 由此把画面适配边界固定为：授权会话 -> JWAE/SCG -> vendor SPICE display/input -> Linux
  窗口；USB storage/HID 为同一会话上的独立 USB/IP forwarding。当前仍缺真实授权会话/首帧/
  USB 样本，未宣称云端互通完成。
- 已构建并安装正式 `ydyun-usbctl 0.2.22-1 amd64`；SHA-256：
  `730375c4b63752e849a5ae827f50a9087204ad7cc7e87ff36b517b79ed28e33f`。44 个单元测试、
  `make -C linux check`、systemd 校验、包审计和 `ydyun-usbctl doctor` 均通过；三个服务均为
  `disabled/inactive`，VHCI 导入列表为空。Debian 规则已固定 `--no-enable --no-start`，安装
  包不会自动启动 USB 接管服务。

## Step 55：真实 Chrome 核验官方客户端来源

- 启动本机真实 `/usr/bin/google-chrome`，在独立 profile 中打开官方客户端页
  `https://soho.komect.com/clientDownload`，页面标题为“下载云电脑客户端”。
- 在真实页面上下文读取官方 API `https://soho.komect.com/cube/h5/user/download/urls/v2/1`，
  返回 `code=2000/SUCCESS`，并再次确认 Windows、UOS AMD64、麒麟 AMD64 的下载标识分别为
  `eeda8cbe9e396866`、`ad2bcdde85d84d6a`、`ce91e6b419aaf831`，与 `doc/client-packages/`
  中的静态分析对象一致。
- 该浏览器核验未登录、未提交凭据、未执行客户端；详细记录见 `doc/CHROME-VERIFICATION.md`。
- 已将核验记录纳入正式 `ydyun-usbctl 0.2.23-1 amd64` 包；SHA-256：
  `44c202d5a764f60b5d71d56b77fe207f4cf20eaeb013d63e3ef6546965a7ec57`。重新安装后三个服务
  仍为 `disabled/inactive`，`doctor` 通过且 VHCI 导入列表为空。

## Step 56：正式包运行时依赖和残留审计

- 对正式 `ydyun-usbctl 0.2.24-1 amd64` 的 control、ELF `DT_NEEDED` 和文件列表做最终审计：
  包的 Depends 仅为 `python3, usbip`；两个 C ABI loader 只依赖 `libc.so.6`，没有链接
  `libjwae`、Qt、Electron、官方 `libusbipd` 或监控库。
- 包文件名审计未发现 `.exe/.dll/.sys/.pdb`、WinDivert、Guard、monitor、QoE、telemetry、
  trace 或 security agent；安装后三个服务仍为 `disabled/inactive`，VHCI 为空，`doctor`
  通过。
- 审计结论已同步进正式包的 `ANALYSIS.md`；本轮包 SHA-256：
  `3a095ab4a04471f38c7d734239c013cdc4f597a42309aa0c00a32159899fc582`。

## Step 57：确认 ZTE 定制 SPICE 库不能进入精简包

- 对官方 `libspice-client-glib-zte-2.0.so.8.5.0` 的 `DT_NEEDED` 和动态符号做定向审计，
  确认 `spice_gtk_get_display_frame_param`、RGB/DMA 取帧、`spice_inputs_*`、
  `avcodec_get_screen_to_client` 和 ZIME data-channel 构成画面/输入数据面证据。
- 同一库直接依赖 `libzxsecurity.so`，并暴露 `spice_gtk_send_qoeagent_msg`、网络质量、
  perfmon/log、insight、远程协助等非核心路径；不能原样复制或通过 SONAME 截取部分 ABI。
- 结论已写入 `doc/CLIENT-ANALYSIS.md` 和 `doc/CLIENT-ABI.md`：正式 Debian 包继续使用
  系统标准 SPICE 下层，不携带官方 ZTE `.so`、`libzxsecurity`、QoE 或 perfmon 依赖。
- 已构建并安装正式 `ydyun-usbctl 0.2.25-1 amd64`；SHA-256：
  `08b5c1fe988f12916744f69ec92939187e3ff8d41af77ddf0256032c0ea6ebec`。44 项测试、包构建、
  `doctor`、服务默认状态和禁止文件审计均通过。

## Step 58：确认 ZIME 是 ZTE 画面厂商传输层

- 对官方 `libZIMEDataEngine.so` 做只读导出/字符串分析，确认它提供 data channel/stream、
  发送接收、事件回调、QUIC/SCTP、外部传输、FEC、证书、cipher、QoS 和告警/统计接口。
- 结合 `libspice-client-glib-zte` 对 ZIME API 的直接依赖，画面链路进一步收敛为
  `JWAE/SCG -> ZIME QUIC/SCTP/外部传输 -> vendor SPICE display/input -> 解码窗口`；
  标准 SPICE framing parser 不能替代该私有建链层。
- `libZIMEDataEngine.so`、ZTE 定制 SPICE 库及其安全/QoE/监控依赖继续不进入 Debian 包；
  本轮只增加静态证据和文档，没有把闭源传输 ABI 猜测写进生产代码。
- 已构建并安装正式 `ydyun-usbctl 0.2.26-1 amd64`；SHA-256：
  `0cc3681ca647ab083f54d1300823b272924aee8e0f129db9dd3a606810bf9534`。44 项测试、包审计、
  `doctor`、服务默认状态和 VHCI 空状态均通过。

## Step 59：统一 Debian 构建入口

- 修复旧的 `build-deb.sh`：不再维护第二套手工 staging/旧版本号，而是读取
  `debian/changelog` 并委托 `dpkg-buildpackage`，确保新文档、loader、systemd 默认禁用规则
  和包审计始终来自同一套 Debian 规则。
- `./build-deb.sh` 已实际执行并生成/安装正式 `ydyun-usbctl 0.2.27-1 amd64`；SHA-256：
  `78fd9a1548e88d0be3629caabb859a4c1c65db64b92a7e49daf4fc71a4f57aa8`。44 项测试、doctor、
  服务状态、VHCI 空状态和禁止文件审计均通过。

## Step 60：把正式包审计接入构建入口

- 新增 `linux/tests/audit_deb.sh`，自动检查 `.deb` 架构、Depends、禁止文件/库名、三个
  systemd 单元和 postinst 不自动启动服务；`./build-deb.sh` 现在构建后必执行该审计。
- 已实际生成并安装正式 `ydyun-usbctl 0.2.28-1 amd64`；SHA-256：
  `21ca67c0369112596bc5cf2435c5ee7b04f981232db987dd2a4c9e5b5e135dc9`。44 项测试、自动包
  审计、doctor、服务默认状态和 VHCI 空状态均通过。

## Step 61：已安装控制器参与 vUDC 键鼠存储闭环

- 扩展 `linux/tests/e2e_vudc_hid.sh` 的 `YDYUN_E2E_CONTROLLER=1` 模式：测试临时创建
  ConfigFS 复合键盘、鼠标和 Mass Storage gadget，通过 `usbipd` 导出，再由已安装的
  `/usr/bin/ydyun-usbctl attach` 导入；原生 `usbip` 模式仍保留作对照。
- root 集成测试实际通过：`usbhid` 识别键盘和鼠标两个接口，`usb-storage` 识别存储接口，
  8 MiB ext4 backing file 完成扇区写入/回读 `cmp`，并向两个 `/dev/hidg*` 写入键盘/鼠标
  报告；退出后 VHCI、gadget、usbipd 和测试配置均清理。
- `linux/Makefile` 的 `package-audit` 现在会按 `debian/changelog` 自动定位当前 amd64
  包；README 补充了安装后控制器模式命令。重新生成并安装的包为
  `ydyun-usbctl 0.2.28-1 amd64`，SHA-256：
  `2a77c330f0b7829422a86cab2aa91673d7794a8620d289ef4de3b746ab7286a1`。
- 当前仍未把真实云端会话标为完成：需要授权的 JWAE/SCG 会话和首帧/USB 端点样本，才能
  把这条已验证的本机 USB/IP 数据面接到中国移动云端私有控制面。

## Step 62：标准 SPICE 画面样本到 Linux 解码器闭环

- 新增 `linux/tests/e2e_spice_display.sh`：本地生成一帧 H.264，把它封装成标准 SPICE
  `REDQ` 链接头以及 `DISPLAY_STREAM_CREATE/DATA` 消息，再调用已安装的
  `/usr/bin/ydyun-ice-probe` 做有界提取。
- `ffprobe` 实际识别提取样本为 `h264,64,48`，`ffmpeg` 成功解码并生成 PNG；测试结束
  后 build 下的样本、wire 文件、JSON 和 PNG 均清理。该测试证明标准 display stream
  到 Linux 解码器的边界，不代表已经完成 JWAE/SCG/ZIME 私有建链。
- `make -C linux e2e-spice-display` 已通过。画面数据路径目前可明确描述为：

  ```text
  授权云端会话/私有传输 -> 标准或厂商 SPICE display frame
      -> stream metadata + encoded sample -> FFmpeg/硬件解码 -> GTK/Wayland surface
  ```

## Step 63：最终安装包回归

- 重新构建并安装 `ydyun-usbctl 0.2.28-1 amd64`；当前包 SHA-256：
  `f30d34928eb61899ce5fad478cb87156ee1d62ea0e7e5630f62fbcb80c615ed7`。
- 使用安装后的 `/usr/bin/ydyun-ice-probe` 完成离线 SPICE/H.264 解码回归；使用安装后的
  `/usr/bin/ydyun-usbctl attach` 完成 vUDC 键盘、鼠标和存储回归。44 项单测、包审计和
  `doctor` 通过；三个 systemd 单元均为 `disabled/inactive`，VHCI 导入列表为空，测试
  gadget、控制器配置和 usbipd 均已清理。

## Step 64：补齐官方 USB 主链路 link-init 只读探针

- 新增 `ydyun-usbctl probe-link`，严格实现已从官方 `libusbipd.so` 静态调用确认的顺序：
  本端发送 `op_common(version=0x0111, code=0x8008, status=0)`（仅 8 字节）-> 等待
  `0x0008` -> 对端发送 `0x8005` -> 本端回 `0x0005` 和设备数量。
- 新增本地 TCP fixture，验证请求字节序、长度和响应顺序；修正了第一次回归中把
  link-init 错写成 12 字节的问题。当前 45 项 Python 单元测试全部通过。
- 该命令只适用于已经由合法 JWAE/SCG 会话提供的已建链 endpoint；不创建认证会话、不
  发送 token、不连接 TLS/KCP，也不会把 `0x8008` 误当作 3246 历史端口协议。

## Step 65：按官方命令方向完成最终包回归

- 根据官方 `handle_usbip_command()` 和 `handle_usbip_req_devicelist()` 反汇编修正
  `probe-link`：link-init 后由对端主动发送 `OP_REQ_DEVLIST`，本端回 `OP_REP_DEVLIST`
  和 4 字节设备数量；本地 fixture 与 45 项单元测试全部通过。
- 重新构建并安装最终 `ydyun-usbctl 0.2.28-1 amd64`；SHA-256：
  `9188a2b3e3f304264f297d551a51ae153799b3e95fc2e7ee8017d70e1a4edd88`。
- 安装后的画面 H.264 离线解码回归、安装后的 vUDC 键盘/鼠标/存储回归、`doctor` 和包
  审计均通过；当前 VHCI 无导入残留。

## Step 66：确认官方 OP_PRE_REQ_IMPORT 控制方向

- 对官方 UOS `libusbipd.so` 的 `handle_usbip_command()`、
  `handle_usbip_pre_req_import()` 和 `handle_usbip_req_import_2()` 做了定向反汇编：
  云端发送 `0x8009` 后附 32 字节 bus ID；本端先回 `0x0009` 状态，成功后再发送
  `0x0003 + 0x140` 字节 import reply，并等待 `0x0003/status=0` 确认。
- 可确认的拒绝状态为设备不存在 `4`、端口禁用 `2`；失败分支会关闭内部链路或清理
  forwarding 状态。该阶段是官方本地 libusb/USB forwarding 的控制面，不是普通 Linux
  `usbip attach` 的替代命令。
- 结论已写入 `CLIENT-ABI.md`：完整云端 USB 适配还需要合法 JWAE/SCG `g_link` 和
  实际 forwarding endpoint；当前 Debian 包继续不猜测认证、状态机或安全策略。

## Step 67：把 OP_PRE_REQ_IMPORT 证据纳入最终包文档

- 重新构建并安装 `ydyun-usbctl 0.2.28-1 amd64`，使最终包内的 `CLIENT-ABI.md` 同步包含
  `0x8009`、状态码和 `0x0003/0x140` forwarding 阶段说明。
- 当前最终包 SHA-256：
  `494f5072b0f78f68b2786986823ff3916b478c0833d502bef3204a65ac62c648`。
- 45 项测试、包审计、安装后 `doctor` 通过；本机三个服务仍保持 disabled/inactive，
  不启动安全、监控或 USB 自动接管。

## Step 68：0.2.29 最终运行回归

- 最终安装包升级为 `ydyun-usbctl 0.2.29-1 amd64`，SHA-256：
  `f7e1c9101810cf7c2dd3e24e867f6d0b4544f6b12bc995796d2a3b04382c2850`。
- 使用该版本实际运行离线 SPICE/H.264 解码测试，以及已安装控制器模式的 vUDC 键盘、
  鼠标、Mass Storage 写入/回读测试；均通过。
- 安装后 `doctor` 通过；三个 systemd 单元均 `disabled/inactive`，VHCI、ConfigFS gadget、
  控制器测试配置和 usbipd 均无残留。

## Step 69：覆盖公开 USB loader 的 JWAE 启停边界

- 为 `ydyun-chuanyun-usb` 增加 fake `cdp_usbip_jwae_set_token`、
  `cdp_usbip_start_jwae_service` 和 `cdp_usbip_stop_jwae_service` fixture，并在
  `make -C linux check` 中显式运行 `jwae_enabled=yes` 配置。
- 回归确认 loader 只按公开 ABI 调用 token/JWAE 启动，再启动 USB service，退出时按顺序
  清理；没有加入日志回调、QoE、监控、SysV 队列或安全策略。

## Step 70：正式构建入口覆盖 JWAE loader 回归

- `./build-deb.sh` 已实际执行新增的 fake JWAE/USB loader 回归；45 项 Python 测试、编译、
  Debian 包审计全部通过。
- 当前安装包仍为 `ydyun-usbctl 0.2.29-1 amd64`，最新 SHA-256：
  `313dd1445a8715425bd83865cba785388de41b9f62da4720694a0977cdf58788`。
- 安装后的 `doctor` 通过，未启动任何 systemd USB 服务，未引入监控或安全库。

## Step 71：区分 vUDC 测试设备与 usbip-host 物理导出

- 尝试用 `YDYUN_E2E_EXPORT=1` 将 `usbip-vudc.0` 交给已安装的 `ydyun-usbctl export`；
  内核实际显示它位于 `/sys/devices/platform/usbip-vudc.0`，不是物理 USB
  `/sys/bus/usb/devices`，控制器按设计拒绝绑定并未改变任何设备状态。
- 因此没有把 vUDC 冒充本地物理 export 测试，也没有为测试解绑当前机器的蓝牙、摄像头或
  鼠标；生产 `export` 仍只接受配置中明确列出的真实 USB busid，并保留 hub 拒绝和驱动
  恢复逻辑。

## Step 72：物理摄像头导出边界与硬件恢复

- 为验证真实 `usbip-host` 边界，曾对明确指定的物理摄像头 `3-8` 做一次可逆导出尝试；
  第一个 UVC 接口解绑成功，第二个接口在设备已变化后返回 `errno=19 (ENODEV)`，因此未
  继续进行导出，也没有触碰键盘、鼠标或蓝牙设备。
- 已删除实验配置，并对 `3-8:1.0/1.1` 精确重新绑定 `uvcvideo`；`/dev/video0/1`、
  `lsusb -t` 的两个 Video/uvcvideo 接口均恢复。该实验不计入物理 USB 转发通过项，说明
  真实设备导出必须增加整设备原子性检查和失败回滚后才能进入生产使用。

## Step 73：加入复合设备回滚并完成最终回归

- `ydyun-usbctl export` 在复合 USB 设备中途解绑失败时会恢复本次已解绑的接口；新增
  部分解绑回滚单测，单元测试增至 46 项。
- 构建并安装 `ydyun-usbctl 0.2.30-1 amd64`；最终包 SHA-256：
  `de2954ee6a4437438db3a8a26c3504fd92f25f5ab4098389737f4bb168e5722a`。
- 安装后的 vUDC -> usbipd -> vhci-hcd 端到端测试通过：键盘、鼠标 HID 报告以及
  Mass Storage 扇区写入/回读均通过；标准 SPICE 显示流 H.264 -> FFmpeg 解码也通过。
- 回归结束后 VHCI 导入为空；本机物理摄像头仍由两个 `uvcvideo` 接口提供
  `/dev/video0/1`，三个 systemd 单元保持 `disabled/inactive`。

## Step 74：补齐官方 USB 存储 drive-map 公共 ABI

- 从官方 UOS `libchuanyun_usbip.so` 的动态符号和反汇编确认：
  `cdp_usbip_drive_map(const char *busid)` 与
  `cdp_usbip_drive_unmap(const char *busid)` 直接把明确 busid 交给 RemoteHub；这与
  `cdpusblib_attach_device/unattach_device` 是不同的存储映射入口。
- `ydyun-chuanyun-usb` loader 增加 `map BUSID`/`unmap BUSID` 命令；fixture 已验证
  attach/detach 与 map/unmap 的显式调用。没有加入官方 deny-list、SysV 消息队列、udev
  注入、设备状态回调或网络质量上报。
- 这补强了官方 USB 存储数据面，但仍保留真实云端前置条件：JWAE/SCG 必须由合法外部
  官方库建立，当前 Debian 包不复制或绕过该认证隧道。

## Step 75：0.2.32 安装版最终闭环

- 最终安装包为 `ydyun-usbctl 0.2.32-1 amd64`，SHA-256：
  `6f1b17683caf9b565c74c8aae891fd5ffb7bd37e383cb01f72bd6f212f135867`。
- 当前安装版本实际通过 vUDC -> usbipd -> vhci-hcd 的键盘、鼠标、Mass Storage
  写入/回读测试；标准 SPICE 显示流 H.264 -> FFmpeg 解码测试通过；安装版 loader 的
  `map/unmap` fixture 也通过。
- 最终状态检查：`doctor` 通过，VHCI 导入为空，三个 systemd 单元均
  `disabled/inactive`，物理摄像头两个接口仍绑定 `uvcvideo` 并提供 `/dev/video0/1`。

## Step 76：精确复核官方 USB/IP import 帧边界

- 从官方 UOS `libusbipd.so` 的 DWARF 类型信息确认：`usbip_usb_device` 为 312 字节，
  `usbip_op_common` 为 8 字节，`usbip_op_import_reply` body 为 312 字节，故
  `OP_REP_IMPORT (0x0003)` 完整控制帧为 320 字节 (`0x140`)；同时记录 busid、busnum、
  devnum 的字段偏移和 32 字节 import request busid。
- 修正 `doc/CLIENT-ABI.md` 与 `doc/USB-FRONTEND-ANALYSIS.md` 对“`0x0003 + 0x140`”的
  解释，避免把完整帧长度与 body 长度重复相加。该工作只更新证据文档，没有执行官方
  闭源库，也没有触碰本机物理 USB 设备。

## Step 77：补齐 Windows 驱动层静态拆分

- 从官方 Windows `cdp_usb.inf` 确认 `VID_28EE&PID_8551`、`CDP Remote USB`、
  `libusbK` kernel service、按需启动和 WDF 1.11；它是虚拟 CDP USB function 的访问
  绑定，不是本地键盘、鼠标或存储类驱动。
- 从 `cdpusbhubhook.sys` 的 PE 依赖和字符串确认 Hub/设备 hook 以及 WMI/ETW 观测痕迹；
  该组件与 USB/IP 数据面无关，已明确排除在 Debian 包之外。
- 结论和 Windows/ZTE 分支对照已写入 `doc/WINDOWS-DRIVER-ANALYSIS.md`，没有在本机
  加载 Windows 驱动，也没有改变物理 USB 绑定。

## Step 78：确认 Electron USB 轮询只是外围信息上报

- 从官方 `app.asar/src/main/usb.js` 确认只通过 Node `usb.getDeviceList()` 轮询 VID/PID、
  接口类和 HID/存储标签，不实现 endpoint、USB/IP 或 URB 数据传输。
- 从 `probe.js` 确认 `peripheralSwitch` 开启后将缓存发送到
  `sc/probe-terminal-portal/peripheral/send/v1`，并与性能/流协议监控开关分开；这些
  逻辑已排除在 Debian 包之外。
- 结论已写入 `doc/CLIENT-ANALYSIS.md`，进一步证明移除安全、QoE、探测和外围上报不会
  影响 Linux `usbip/vhci/usbhid/usb-storage` 数据面。

## Step 79：校正官方 USB 服务启动函数的回调 ABI

- 通过 `libchuanyun_usbip.so` 包装层、`libusbipd.so` 的 `rh_set_on_init` 调用和官方
  `chuanyun-redirect` 汇编交叉验证，`cdp_usbip_start_service` 的准确签名是
  `int (const char *, void (*)(int))`；此前把第二参数写成 `int *` 是错误的 ABI 假设。
- 已修正 `ydyun-chuanyun-usb` 与 fake fixture：调用官方库时传 `NULL`，不注册状态/监控
  回调，避免真实库按函数指针调用整数地址；后续以完整 `make -C linux check` 和 Debian
  包审计验证。

## Step 80：完成 ABI 修正后的打包与端到端回归

- `make -C linux check` 通过：C loader/fake ABI、会话回调、USB 操作与 46 个 Python
  单元测试全部通过；Debian 包审计通过并生成 `ydyun-usbctl 0.2.33-1 amd64`。
- 已安装并完成只读健康检查：架构为 x86_64，`usbip` 与 `vhci-hcd` 可用，三个服务均为
  `disabled/inactive`，无残留 VHCI 导入；物理键盘、摄像头、鼠标和蓝牙仍由
  `usbhid/uvcvideo/usbhid/btusb` 管理。
- vUDC→usbipd→vhci-hcd 端到端验证通过键盘、鼠标 HID 和 Mass Storage 读写；SPICE 显示
  流验证通过有界抽取与 FFmpeg 解码。测试结束后 VHCI 恢复为空。
- 当前可交付的是 Linux 本地 USB/IP、HID/存储转发、标准 SPICE 解析/验证及精简厂商 ABI
  loader；真实中国移动云电脑云端会话仍依赖合法 JWAE/SCG 参数或首帧/URB 样本，未伪造
  认证，也未把安全、监控、QoE、遥测和闭源安全库放入 Debian 包。

## Step 81：反向确认官方 viewer 启动 URL 与画面入口

- 对官方 UOS `libchuanyun.so` 的 `connectVm` 异步线程和 `chuanyun-view` `main`/`parseURL`
  做了静态交叉验证：云端 VM ready 后，控制库拼出
  `./chuanyun-view spice://127.0.0.1:10800+...` 并启动 viewer；viewer 再进入 vendor
  SPICE/JWAE，而不是由 Electron 或 USB/IP 直接绘制画面。
- 由官方 viewer 的 `spice_main_channel_update_display`、display surface/stream、
  `spice_inputs_channel_*`、`spice_session_set_vmid`、`jwae_*` 和 `StartCdpUSB` 符号确认：
  画面、桌面键鼠和 USB redirect 是共享会话前置下的独立数据面。
- `linux/src/ydyun_spice.py` 新增有界 `parse_viewer_connection_url()`，只解析
  `spice://host:port+opaque...` / `spice-conn://...` 的安全摘要；不解密、不执行官方
  `system()`、不输出后缀字段、不复制 JWAE/SCG、安全、QoE、监控和策略队列。
- 完整 Python 测试增至 48 项并通过；后续 Debian 0.2.34-1 构建/审计会再次验证安装版。
  真实云端首帧和远端 URB 仍需授权会话样本，当前不把静态 ABI 证据冒充云端联调完成。

## Step 82：还原官方 viewer URL 后缀的字段来源

- 从官方 `libchuanyun.so` 的 `ConnectInfo::serialize` 静态恢复字段名：`traceId`、
  `scAuthCode`、`hostIp`、`scgIp`、`scgIpv6`、`scgTcpPort`、`scgUdpPort`，并确认对应
  `std::string` 成员偏移 `0x20` 至 `0xe0`。
- 从连接线程反汇编确认 viewer 命令后缀的顺序包含 `scgTcpPort`、`scgUdpPort`、
  `scAuthCode`、`traceId` 以及两个 `InitNamedPipe` 相关的线程字符串；这些字段用 `+`
  连接，且认证码/随机值不能记录或交给 shell。
- 该结果强化了“官方 viewer URL 是会话内部边界，不是公开登录协议”的判断；生产 parser
  继续只暴露 host/port/字段数量，后缀保持 opaque。文档已补充字段偏移和证据来源。

## Step 83：最终安装版数据面回归

- 重新构建并审计 `ydyun-usbctl 0.2.34-1 amd64`，48 个 Python 单元测试、C ABI fixture
  和包内容审计均通过；最终包 SHA-256 为
  `098ac6682dd49d0f73b854ae8b89bfe30026dbe4cb043f5d70dcea6215ca9fe1`。
- 安装版 vUDC→usbipd→vhci-hcd 回归通过键盘、鼠标 HID 和 Mass Storage 读写；标准
  SPICE 显示流抽取并经 FFmpeg 解码通过。测试后 VHCI 导入为空。
- 最终物理机状态保持安全：三个 systemd 单元均 `disabled/inactive`，物理键盘/鼠标的
  `usbhid`、摄像头的 `uvcvideo`、蓝牙的 `btusb` 绑定未改变。

## Step 84：补齐 Electron native addon 与 viewer URL 的字段证据

- 从官方 `chuanyunAddOn/ccsdk/src/simpleasyncworker.h` 确认 Electron native addon 的
  `vmInfo` 输入字段：服务器/日志服务器、终端、unit type、VM ID、用户名、auth code、
  biz code 和 node 地址；`jsCysdk.js` 只负责把命令与参数交给 native addon。
- 从官方 `chuanyun-view::parseURL` 反汇编确认 URL 后缀索引 1、2、5、6、7、8 写入
  字符串成员，索引 3、4 做十进制整数解析，索引 0 保留为 Qt 字符串；前缀中的
  `spice://127.0.0.1:10800` 是索引 0。
- 这条证据链闭合为 Electron `vmInfo` → public `libchuanyun.so` → JWAE/SCG → viewer
  URL → SPICE display/input + 独立 USB redirect。Debian 适配不复制 Electron 监控、
  外设轮询、QoE、日志和安全路径，生产 parser 继续只暴露安全 endpoint 摘要。

## Step 85：标准 GTK viewer 接受官方 URL 边界

- `linux/experimental/ydyun_spice_viewer.c` 新增 `spice://HOST:PORT+opaque...` 和
  `spice-conn://HOST:PORT+opaque...` 输入模式；只解析首段 endpoint，后缀不打印、不执行、
  不交给 vendor 库。
- 使用 Debian `spice-client-gtk-3.0` 编译并在 `xvfb-run` 下对无服务端口的
  `spice://127.0.0.1:65534+opaque+session` 完成连接错误和清理 smoke test；这验证了
  官方 URL 可以进入 Linux 标准 SPICE display/input harness。
- 该 viewer 仍是实验性构件，不进入生产包、不负责 JWAE/SCG 登录；真实首帧和键鼠上行
  还需要授权会话端口提供标准或厂商 SPICE 数据。

## Step 86：0.2.35 安装版交付

- Debian 包重新构建、审计并安装为 `ydyun-usbctl 0.2.35-1 amd64`；48 个 Python 测试、
  C ABI fixture、包内容审计和安装后 `doctor` 均通过。
- 最终包 SHA-256：
  `13c2883194f8eb4ea76b6fe104cdec0f25091631f5a0a22e43f0bdddb5352d9d`。
- 包内文档已同步 Electron addon、viewer URL 索引和标准 GTK viewer 使用说明；生产包
  仍不携带官方闭源 viewer、JWAE、ZIME、安全、监控或 QoE 库。

## Step 87：增加授权 session 到 public SDK 的安全配置桥

- `ydyun-session-probe` 新增可选 `--emit-loader-config`，从已经解码的授权 JSON 映射
  `vmId`、`vmUserName`、`authCode`、`bizCode` 和显式 SDK 控制服务器字段，创建一次性
  `0600` 配置供 `ydyun-chuanyun-session` 使用。
- 该路径不登录、不解密、不联网、不回显 auth/token，不从脱敏输出恢复秘密；目标文件
  已存在时拒绝覆盖，也不会自动启动 USB、画面或监控服务。
- 完整 Python 测试增至 50 项并通过；真实云端连接仍需操作者已经合法取得的解码响应和
  官方 public library，当前没有伪造认证或执行闭源库。

## Step 88：0.2.36 安装版验证

- Debian 包重新构建、审计并安装为 `ydyun-usbctl 0.2.36-1 amd64`；安装后的
  `ydyun-session-probe --help` 已确认安全配置桥参数存在，`doctor` 通过。
- 最终包 SHA-256：
  `a39db51aab26620dfeca5a03177a1c3ae57c2dc95d70a8fb4b95ea2893026848`。
- 三个 systemd 单元仍为 `disabled/inactive`，VHCI 无导入，物理 USB 驱动绑定未改变。

## Step 89：确认官方 root USB 策略前端并排除

- 静态核对 UOS `chuanyun-redirect.service`、启动脚本和 ELF 字符串：该服务以 root 运行，
  失败自动重启，并包含 udev 权限规则注入、SysV 消息队列、cJSON USB 状态回报、deny/allow
  策略和设备监控回调。
- 结论是这些属于安全/监控/策略控制面；USB/IP 报文、usbip-core、vhci-hcd、usbhid 和
  usb-storage 才是可独立验证的数据面。Debian 包继续不安装该服务，不改 udev、不建策略
  队列、不上报设备状态，改用显式 busid 和标准内核 USB/IP。
- 已把证据和边界写入 `doc/CLIENT-ANALYSIS.md`、`doc/CLIENT-ABI.md`；下一步重新构建
  安装版以同步包内文档，再保持物理 USB 和云端授权边界不变。

## Step 90：0.2.37 安装版与数据面回归

- Debian 包重新构建、审计并安装为 `ydyun-usbctl 0.2.37-1 amd64`；50 个 Python 测试、
  C loader fixture、包审计和安装后 `doctor` 均通过。
- 最终包 SHA-256：
  `6f1f5ae348307370a33dd33331bcd9a7b9a9aae53acc881212f97227af52be8d`。
- vUDC → usbipd → vhci-hcd 回归再次通过键盘、鼠标 HID 和 Mass Storage 读写；标准 SPICE
  display stream 抽取并经 FFmpeg 解码通过，测试后 VHCI 无导入。
- 三个 systemd 单元仍为 `disabled/inactive`；物理 USB 树仍由 `usbhid`、`uvcvideo`、
  `btusb` 负责键鼠、摄像头和蓝牙接口。真实云端首帧、USB/IP URB 和厂商 JWAE/SCG 会话
  仍保留为下一阶段的授权样本验证项。

## Step 91：将标准 SPICE 显示/桌面键鼠入口纳入 Debian 包

- 将原实验性 `ydyun_spice_viewer.c` 接入构建和安装流程，包内提供可选的
  `ydyun-spice-viewer` 命令；它创建标准 `spice-gtk` 窗口，接收 display channel，并由
  GTK widget 处理桌面键盘/鼠标输入。
- viewer 支持普通 `HOST PORT [TLS_PORT]` 和官方 `spice://HOST:PORT+opaque...` 边界，
  只解析首段 endpoint，不登录、不解密、不执行后缀、不加载 JWAE/安全/监控/遥测库。
- 该入口让标准 SPICE 的画面和桌面键鼠具备实际 Linux 可执行路径；真实中国移动云端仍需
  合法 JWAE/SCG 会话先产生可访问端口，不能用 viewer 的本地错误路径冒充云端互通。

## Step 92：0.2.38 viewer 依赖与安装后审计

- Debian 包重新构建、安装并审计为 `ydyun-usbctl 0.2.38-1 amd64`；最终包 SHA-256：
  `ce13e37c26ddd74545951821a3618588be3dd0c36394508421782a3b9d0a766c`。
- `ldd /usr/bin/ydyun-spice-viewer` 只解析到系统 GTK、SPICE、GStreamer、OpenSSL 和
  标准 USB-redirection 依赖；动态段没有 `libjwae`、ZIME、厂商安全/QoE/监控库。
- 安装版 viewer 的参数错误、无服务端 URL 清理路径、USB HID/storage 闭环、SPICE H.264
  抽取解码、50 项 Python 测试、包审计和 `doctor` 均通过。
- 三个服务仍为 `disabled/inactive`，VHCI 无导入，物理键鼠/摄像头/蓝牙接口保持原驱动；
  官方 Windows/UOS/Kylin 包仍只作为 `doc/` 下静态行为参考，不被清洁 Debian 包执行。

## Step 93：3246 控制帧到真实 USB/IP 的本地闭环

- 新增测试云端代理 `linux/tests/backward_cloud_proxy.py`：只发送已确认的
  `0x0202`、`command=1`、busid 控制帧，然后把桥接后的字节流接到真实 `usbipd`，不处理
  认证、不伪造 JWAE/SCG、不接触物理 USB。
- `YDYUN_E2E_BACKWARD=1 ./linux/tests/e2e_vudc_hid.sh` 已通过：桥日志确认收到
  `busid=usbip-vudc.0` 并建立 local USB/IP import 配对，VHCI 最终识别 boot keyboard、
  boot mouse、Mass Storage，存储读写通过。
- 这证明了“已建立的 vendor 控制连接 → 标准 USB/IP/VHCI 数据面”的 Linux 桥接方向；
  仍不能替代真实云端 JWAE/SCG 建链和真实 URB 样本，默认 compression 继续保持 `off`。

## Step 94：0.2.39 安装版桥接回归

- Debian 包重新构建、审计并安装为 `ydyun-usbctl 0.2.39-1 amd64`；最终包 SHA-256：
  `ed39aea899ac026a7b7ac960cd7e8bc3dac50949410c71486b229efc6d3aa1d7`。
- 使用安装版重新通过 `make -C linux e2e-vudc-backward`：控制帧到标准 USB/IP/VHCI 的
  实际本地闭环、键鼠 HID 和 Mass Storage 读写均通过；SPICE display 解码和 GTK viewer
  无服务端清理路径也通过。
- `ydyun-usbctl doctor` 通过，三个 systemd 单元均 `disabled/inactive`，测试结束后 VHCI
  无导入，物理键盘、鼠标、摄像头和蓝牙接口仍由原生驱动绑定。

## Step 95：补齐 backward service 的显式配置边界

- 修正 `linux/etc/ydyun-usb-bridge.conf`：新增完整 `[backward]` 段，包含已验证的
  `allowed_ops=1,2`、`compression=off`、`auto_attach=no` 和 loopback 监听地址。
- 该配置解决了“显式启用服务却因缺少 backward 段启动失败”的部署问题；systemd 单元
  仍由 Debian 安装脚本保持 `disabled/no-start`，不会自动监听或暴露 USB/IP 端口。

## Step 96：0.2.40 配置修正版安装验证

- Debian 包重新构建、审计并安装为 `ydyun-usbctl 0.2.40-1 amd64`；最终包 SHA-256：
  `b96f53d37d5a7efb98bd9e61c5b1127d5ee2ff074109f8cef0c0dac06b674644`。
- 安装后的 `/etc/ydyun-usb/ydyun-usb-bridge.conf` 可被 `load_backward_settings()` 正确
  解析；`doctor`、安装版 backward 闭环和 VHCI 清理均通过。
- 三个 systemd 单元仍为 `disabled/inactive`，无监听端口、VHCI 无导入；物理 USB 树的
  `usbhid`、`uvcvideo`、`btusb` 绑定保持不变。

## Step 97：区分 vUDC 测试设备与 usbip-host 本地导出

- 只读检查确认 `usbip list --local` 只列出物理 USB 总线设备；`usbip-vudc` 复合 gadget
  由 `usbipd --device` 提供服务，不会以 `usbip-host` busid 出现，因此不能用它冒充本地
  物理导出路径的实测证据。
- 已撤掉不具备证明力的 vUDC export 实验分支；生产代码仍只允许 `[local_devices]` 显式
  指定物理 busid，执行前拒绝 hub、释放的接口失败会恢复原驱动，默认导出服务保持关闭。
- 本地 USB → 云端方向的协议承载仍是标准 `usbip-host`/`usbipd`；不在未授权、会解绑真实
  键鼠的情况下自动测试，待隔离的真实 USB 存储设备或授权云端样本再验证。

## Step 98：dummy_hcd 隔离验证本地 USB 上行导出

- 为 root-only `linux/tests/e2e_vudc_hid.sh` 增加 `YDYUN_E2E_DUMMY_HOST=1` 和
  `make -C linux e2e-dummy-host`：使用 `dummy_hcd` 将 ConfigFS 复合 HID+Mass Storage
  gadget 枚举成真实 Linux USB busid（本次为 `21-1`），再由安装版 `ydyun-usbctl export`
  释放 `usbhid`/`usb-storage` 并绑定 `usbip-host`。
- 本次实际通过 `usbip-host → 普通 usbipd → vhci-hcd`：远端枚举出 3 接口复合设备，键盘和
  鼠标接口由 `usbhid` 接管，存储接口由 `usb-storage` 接管，8 MiB 虚拟盘远端读写校验通过。
  这首次验证了本项目“本地 USB → 云端 USB/IP”生产代码路径，同时没有解绑任何物理 USB。
- `usbipd --device` 仅适用于 `usbip-vudc`，不能用于 `usbip-host`；dummy-host 模式已改用
  普通 `usbipd`。`dummy_hcd` gadget 端 `hidg` 写入在本内核下可能因无待处理 interrupt-IN
  URB 阻塞，测试已改为 2 秒有界探针并如实记录；vUDC 模式的 HID 报告完整回归仍通过。
- 测试结束后确认 VHCI 无导入、ConfigFS gadget 已删除、`dummy_hcd` 已卸载；物理 USB 树仍由
  `usbhid`、`uvcvideo`、`btusb` 负责，三个 systemd 单元继续 `disabled/inactive`。

## Step 99：0.2.41 安装版交付验证

- Debian 包重新构建、审计并安装为 `ydyun-usbctl 0.2.41-1 amd64`；包 SHA-256：
  `32944ae07d25c2f1dbc004a4710d447144cc2f29c73b42449611eb49c247ce93`。
- 构建阶段的 50 项 Python 单测、C loader/viewer 编译、安装版 `doctor`、vUDC 完整 HID+
  Mass Storage 回归和 dummy_hcd 本地导出回归均通过；后者使用普通 `usbipd` 验证了
  `ydyun-usbctl export/unexport` 的 `usbip-host` 路径。
- 安装后 `ydyun-usbctl doctor` 通过；`ydyun-usbctl.service`、`ydyun-usbctl-backward.service`
  和 `ydyun-usb-export.service` 均为 `disabled/inactive`，VHCI 无导入。清理了一个此前遗留的
  项目测试 `usbipd` 3248 监听，当前不再有本项目测试端口监听。
- 物理 USB 仍保持原状：键盘/鼠标为 `usbhid`，摄像头为 `uvcvideo`，蓝牙为 `btusb`；未把
  任何实体设备用于上行 export 回归。

## Step 100：官方 usbredirect 数据面与策略面再次分离

- 重新核对官方 ZTE/UOS `usbredirect`：其 ELF 直接依赖 `libzxsecurity`、`libqoelog`、
  `libusbtrace`，同时包含 `usbip_send_op_common`、`usbip_xmit`、`stub_rx_loop`/`stub_tx_loop`
  和 sysfs `usbip_sockfd` 路径，证明它把本地设备导出到 USB/IP 数据面；CAG/HTTP、URB、
  import/unimport 和设备状态属于其会话控制层。
- `usbip.conf` 的官方 deny 规则包含 Boot HID 和 Hub，错误字典还提示不建议键鼠重定向；
  这些是厂商策略而不是协议限制。Linux 适配保留“拒绝 Hub/root hub + 显式 busid”的安全
  边界，但不复制官方对 HID/存储的 deny-list，符合用户需要键盘、鼠标和 USB 存储转发。
- `monitor_qoe.sh`/`monitor_sohosdk.sh` 对 `usbredirect` 的重启、SUID 和 QoE/trace 依赖
  继续不进入 Debian 包；标准 `usbip-host`/`usbipd`/`vhci-hcd` 数据面不依赖这些组件。
- 真实 Chrome 复核确认官方网站页面标题为“下载云电脑客户端”，当前 Chrome 153 的 CDP
  页面为 `https://soho.komect.com/clientDownload`；对应下载标识和包哈希保存在
  `doc/CHROME-VERIFICATION.md`，没有登录或提交凭据。

## Step 101：0.2.42 文档同步安装验证

- Debian 包重新构建、审计并安装为 `ydyun-usbctl 0.2.42-1 amd64`；包 SHA-256：
  `5b167595557d6923f8d76a46058407b2caabecf2b48354464df161e2b95a2124`。
- 安装后 `doctor` 通过，`CLIENT-ANALYSIS.md.gz` 已包含 `usbredirect` 的数据面/策略面
  分离证据；三个 systemd 单元仍为 `disabled/inactive`，VHCI 无导入。
- ConfigFS 测试 gadget、`dummy_hcd` 和项目测试端口均已清理；物理 USB 仍保持
  `usbhid`、`uvcvideo`、`btusb` 原生绑定。

## Step 102：通过真实 Chrome 下载并复核当前官方 UOS 包

- 直接控制本机真实 Chrome 153，刷新官方下载页并在页面上下文读取接口；接口返回的 UOS
  AMD64 标识仍为 `ad2bcdde85d84d6a`，页面标题为“下载云电脑客户端”。
- 从该真实入口下载 `CMCC-JTYDN-UOSx86-2.23.1.deb` 到当前 `doc/client-packages/`
  目录，包大小 246700884 字节，SHA-256 为
  `65fa5a093d73bfef407304b32ed45a1c761d4b105ebdcfa2ffea67e9de751202`，已完成解压。
- 当前包版本为 `cmcc-jtydn 2.23.1`；关键 Electron、SPICE、USB/IP 和 ZTE 二进制与
  先前样本逐字节相同，没有发现新的 USB/画面协议实现。
- 当前官方包仍含 `libzxsecurity`、`libqoelog`、`libusbtrace`、`usbredirect` 等安全/
  监控/QoE 控制面组件；清洁 Debian 包继续不携带这些组件，仅实现标准 USB/IP、HID/
  Mass Storage 测试闭环和标准 SPICE viewer。

## Step 103：真实登录、画面首帧、键鼠输入和 USB 通道交叉验证

- 用户完成官方 Linux 客户端二维码登录；真实云列表出现一个运行中的 `zte-cloud-pc` 云
  电脑。官方连接控制面成功返回连接字符串并启动 ZTE `uSmartView_VDI_Client`。
- 首次启动因 Debian 缺少公开 `libqt5multimedia5`/`libqt5concurrent5t64` 以退出码 127；
  补齐运行库后重启官方客户端，真实 viewer 成功显示云电脑 Windows 锁屏画面。实际日志
  记录 `display-2:0` H.264 IDR/FFmpeg 解码、`1920x1080` 画面和 `cursor-4:0` 光标通道；
  本地截图为 `build/cmcc-smartview.png`。
- `inputs-3:0` 初始化成功且服务端声明支持鼠标移动；无害 Shift 按键的按下/释放和扫描码
  事件进入 viewer 日志，真实验证了键盘输入路径。没有执行远端点击、登录或其他副作用操作。
- viewer 的 `usbMsgClient` 调用了自动 USB 重定向并读取 USB 策略；真实会话中观察到
  `port=3246/type=2/linkSource=117` 的 USB IPC 注册头、CAG out-of-band proxy 和 USB
  bandwidth link。官方 `usbredirect` 服务保持禁用，`usbip port` 为空，实体键鼠/摄像头/
  蓝牙未被解绑或导出。
- 结论：画面、光标、键鼠输入与 USB/IP 风格转发是相互独立的通道；清洁包继续只带标准
  USB/IP、HID、Mass Storage 和 SPICE 解析/查看器，明确不带安全、监控、QoE、trace、
  `usbredirect` 或 root 策略服务。下一步是把已授权的会话代理以可审计方式接到标准 USB/IP
  数据面，仍不得自动接管实体设备。
- 文档证据同步进 `0.2.45-1` Debian 包；52 项 Python 单测、C loader/viewer 编译、包审计、
  `dummy_hcd -> usbip-host -> usbipd -> vhci-hcd` 的 HID+Mass Storage 回归均通过。最终安装
  后 `doctor` 通过，三个清洁 systemd 单元和官方 `zqoe`/`chuanyun-redirect` 均为
  `disabled/inactive`，VHCI 无导入，实体 USB 仍由 `usbhid`、`uvcvideo`、`btusb` 接管。

## Step 104：官方 USB IPC 本地头、代理前置和真实导入数据面

- 结束官方客户端实验后再次核验：官方客户端、`uSmartView_VDI_Client`、`bootCypc` 和
  `usbredirect` 均已退出，没有 3240/3246/45253/51000 监听；`usbip port` 为空，实体
  键盘、鼠标、摄像头和蓝牙分别恢复为 `usbhid`、`uvcvideo`、`btusb` 原生绑定。
- 对官方 `usbredirect` 的未剥离符号和调用点做静态交叉验证：本地 viewer IPC 的前 16 字节
  是小端 `uint32 cmdType, clientSocket, clientPid, msgLen`；`cmdType=1` 请求设备列表，
  返回 `cmdType=2` 的 16 字节头加设备信息载荷。实验捕获只保留字节数、SHA-256 和 16 字节
  前缀，没有保存认证材料或 USB 载荷。
- 官方独立 `usbredirect` 在 loopback `3240` 提供本地控制入口，viewer 会探测一段
  `3240`–`3250` 范围；真实会话日志中的 `port=3246/type=2/linkSource=117` 是 USB link
  的会话/路由字段，不能把裸本地端口当成云端授权入口。
- `sendProxyMesgHead` 静态显示 vendor 到云端 USB endpoint 前先发送固定 `0x74` 字节
  前置头，其中包含版本/类型和会话目的地元数据；随后真实摄像头导入日志出现标准
  `op_common -> DEVLIST -> IMPORT -> stub_rx/stub_tx/URB`。这证明 USB 数据面不是自定义
  设备协议，而是“厂商会话前置 + 标准 USB/IP”。
- 真实导入期间官方自动策略选择了摄像头并拒绝键盘/鼠标；这是策略面而非协议限制。清洁
  Debian 包不实现该自动策略，继续使用显式 busid、拒绝 Hub/root hub 的安全边界，以便
  用户明确选择 USB 存储、键盘和鼠标。
- 新增仅 loopback、只读元数据的 `linux/tests/capture_local_usbipc.py` 及 2 项头解析测试；
  它不是服务、不会枚举 USB、不会转发或落盘 USB 载荷，也不进入 Debian 运行时。
- 结论更新：官方画面/光标/键鼠输入、USB IPC 本地控制头、厂商代理前置和标准 USB/IP
  数据面均有真实或静态证据；清洁包仍不携带 JWAE/SCG、安全、QoE、trace、监控和
  `usbredirect`，尚未把厂商私有会话前置猜测性接入生产包。

## Step 105：0.2.45-1 最终包和物理机清理复核

- 以本轮文档、协议头解析回归和现有适配代码构建 `ydyun-usbctl 0.2.45-1 amd64`；
  52 项 Python 测试、C loader/viewer 编译、包审计均通过。
- 最终包 SHA-256：
  `31c9dd35495e5c0b6dfcdcd28c05059d347d4e7fc94777586c07c173e16cc96c`；包内运行时依赖
  只有 `python3`、`usbip` 和 Debian `spice-gtk`，未发现官方安全/QoE/trace/监控或
  `usbredirect` 文件。
- 安装包后的 `doctor` 通过，`dummy_hcd -> usbip-host -> usbipd -> vhci-hcd` 复合
  HID+Mass Storage 回归再次通过；测试 gadget、测试 usbipd、VHCI 导入和监听端口均已
  清理。
- 最终物理机状态：官方客户端和 viewer 无残留进程，官方及清洁 systemd 单元均为
  `disabled/inactive`，`usbip port` 为空，键盘/鼠标/摄像头/蓝牙仍由 `usbhid`、
  `uvcvideo`、`btusb` 接管。

## Step 106：0.2.46-1 真实虚拟 U 盘导入与最终回归

- 在物理 Debian 主机上只创建 dummy_hcd/ConfigFS 虚拟 Mass Storage（`21-1`），用 mount
  namespace 将它暴露给官方 `usbredirect`；官方 viewer 重新连接后真实完成导入，日志依次
  出现代理前置、私有 import、`op_common`、`DEVLIST`、`IMPORT`、`stub_rx/stub_tx` 和 URB。
- viewer 收到虚拟设备 `result=0`、`redirectStatus=2`，说明官方云会话的 USB 数据面已真实
  接通；测试结束后立即关闭官方进程和虚拟 gadget，没有把实体键鼠、摄像头或蓝牙交给它。
- 清洁包新增 `build_vendor_proxy_header()`，只序列化已验证的 0x74 字节前置头，不生成凭据、
  不连接云端、不猜测私有 0x500 import payload；默认运行时仍使用显式标准 USB/IP 数据面。
- 0.2.46-1 构建、安装、审计通过；54 项 Python 测试、C loader/viewer 构建、`doctor` 和
  安装版 `dummy_hcd -> usbip-host -> usbipd -> vhci-hcd` HID+Mass Storage 回归通过。
- 最终包 SHA-256：`c523b57a134a3540d6bef30b848a79fc7d6ee11794ea702abc3e2caacfc7e59b`。
- 最终清理复核：三个清洁服务及官方 `zqoe`/`chuanyun-redirect` 均 disabled/inactive；
  无 3240/3246/40955/51000/3261 监听，无官方进程，`usbip port` 为空，实体 USB 恢复为
  `usbhid`、`uvcvideo`、`btusb` 原生绑定。

## Step 107：恢复官方 USB 前端、JSON 和 JWAE 分层

- 静态确认 `chuanyun-redirect` 通过 SysV IPC 接收设备/服务命令，调用
  `cdp_usbip_start_service(g_json, NULL)`；类型 1/2/3/4/9 分别对应配置启动、停止、
  busid 映射、解除映射和重新枚举。
- 从 `libusbipd.so` 的 `rh_server_config_update` 恢复官方 JSON 字段：
  `config_version`、`maxWidth`、`maxHeight`、`hotplug_support`、`use_scg`、`port`、
  `ip`、两组 allow/deny 列表和 `vimId`；这些字段已写入 `CLIENT-ANALYSIS.md`，但不被
  清洁包作为登录凭据或默认配置使用。
- 从 `api_android_start_jwae_service` 恢复 `jwae_start` 的 `0xd8` 配置边界，确认本地
  `10800` viewer 端口和回环代理属于授权会话运行时；token、密码及私有 import 仍不猜测。
- 本轮只改动分析文档和包内 changelog，未启用官方服务、未接管物理 USB，也未把安全、
  监控、QoE、trace 或闭源库加入 Debian 运行时。

## Step 108：官方 USB ABI loader 的惰性加载验证

- 将显式外部库 loader 的 `dlopen` 改为 `RTLD_LAZY`，避免 USB-only 枚举在启动时被官方
  视频编码器符号阻塞；正常使用前仍通过 `dlsym` 检查所有 USB 入口。
- 使用官方 UOS `libchuanyun_usbip.so` 与同包 `libjwae.so` 做无会话 `list` 验证，进程
  返回码为 0、空设备列表符合预期；没有启动官方客户端、私有会话或安全/监控服务。
- 这证明可选 ABI 边界在 Debian x86_64 上可以加载，未证明云端授权、私有 import 或真实
  远端 URB 已由清洁包独立完成；后者仍需合法控制面提供运行时字段。

## Step 109：0.2.48 安装版交付与最终回归

- Debian trixie amd64 包 `ydyun-usbctl 0.2.48-1` 已安装；SHA-256 为
  `9e5dd1ff3c1bb3c2aaac773f4d7856150d6a52aacd75e95d19950f3bc2ab9405`。
- 54 项 Python 测试、C loader/viewer 构建、包审计、安装版 `doctor` 和安装版
  `dummy_hcd -> usbip-host -> usbipd -> vhci-hcd` HID+Mass Storage 读写回归均通过。
- 最终验收保持服务默认 disabled/inactive，官方安全/QoE/监控/trace/usbredirect 不在包内；
  真实云端私有登录建链和 0x500 import 仍明确留在合法会话适配边界，不作未验证承诺。

## Step 110：已登录官方会话的画面/输入/USB 实时边界

- 用户在真实桌面点击连接后，官方 UOS 客户端恢复画面会话；脱敏日志确认 1920×1080
  显示帧、H.264/SPICE 解码与渲染，以及 SPICE 鼠标位置、按下、释放事件。
- 官方 USB 模块在同一会话收到自动 USB/TWAIN/打印机策略调用，并连接到独立的会话端点；
  该端点是运行时字段，不是固定的公网 USB/IP 入口。
- 对官方本地 51000 监听进行了 8 秒标准 viewer 短测：未形成第二个持续 SPICE 会话，官方
  连接未被影响。这确认 51000 是厂商 VDI/控制封装入口，清洁 viewer 不能绕过官方登录、
  会话协商和私有控制层直接复用。
- 适配结论：画面与键鼠输入按 SPICE/VDI 通道处理；USB 存储和 USB HID 按独立 USB/IP
  通道处理。清洁包保留标准 SPICE viewer、标准 USB/IP、VHCI 和显式设备选择，继续排除
  安全、QoE、trace、监控和厂商 root 策略组件。

## Step 111：0.2.49-1 文档同步包

- 将本轮已登录官方会话的画面/输入/USB 边界结论同步进 Debian 文档，构建并安装
  `ydyun-usbctl 0.2.49-1`。
- 54 项测试、C loader/viewer 构建、安装版 `doctor` 和 Debian 包审计通过；本次只更新
  changelog/文档，USB/IP、VHCI、标准 SPICE viewer 代码未改变。
- 当前安装版再次完成 `dummy_hcd -> usbip-host -> usbipd -> vhci-hcd` 复合 HID + Mass
  Storage 枚举和存储读写回归，测试 gadget、usbipd 和 VHCI 导入随后已清理。
- 当前包 SHA-256：
  `64e45de051a051ce6b3fee5063f50af3806b584eea6f1d02368d0610b27cec1a`

## Step 112：Debian 云电脑端 KDE 图形链路恢复

- 远端确认是 Debian 13 trixie x86_64；安装 `task-kde-desktop`、`kde-plasma-desktop`、
  `sddm`、`spice-vdagent` 和已有的 `qemu-guest-agent`，设置默认目标为
  `graphical.target`。
- 初始云内核只有 `+deb13-cloud-amd64`，缺少 QXL DRM 模块，导致 SDDM active 但没有
  Xorg/Wayland，官方客户端只能看到文本控制台。安装 Debian 通用
  `linux-image-amd64` 后，切换到 `6.12.107+deb13-amd64`，已确认加载 `qxl` 并出现
  `/dev/dri/card0`。
- SDDM 配置为 X11；重启后远端已运行 Xorg、SDDM greeter、PipeWire/WirePlumber，且有
  QXL 虚拟显卡和虚拟声卡设备。官方客户端日志已出现重启后的 H.264/SPICE 帧和鼠标
  事件，纯 CMD 显示问题已解决。
- 当前等待用户在 SDDM 图形界面登录普通账号后，继续验证 Plasma 用户会话中的
  `spice-vdagent`、键盘/鼠标和声音；没有向远端安装暂缓的 Linux 客户端控制面。

## Step 113：Plasma 会话和官方画面/输入/音频验收

- 普通账号 `cloud` 已登录，Plasma Wayland 会话 active；`plasmashell`、PipeWire、
  WirePlumber 和用户态 `spice-vdagent` 均正常运行。
- 远端 PipeWire 枚举到内置虚拟声卡和默认立体声 sink；通过 `pw-play` 播放短测试音返回
  成功，官方客户端日志同步出现 48kHz 双声道 `playback_start`/`playback_stop`，确认
  Debian 会话到官方客户端的声音链路可用。
- 官方客户端日志持续出现 H.264/SPICE 画面帧、鼠标移动/点击和键盘扫描码事件；至此
  Debian 虚拟机端的画面、鼠标、键盘、声音基础链路已完成实机验证。

## Step 114：官方客户端全屏/还原分辨率行为复核

- 客户端全屏/还原按钮实际走 `resizeEvent -> modify_monitor_send_to_spice ->
  spice_main_send_monitor_config`；全屏时发送 `1920x1080`，窗口最大化过渡阶段会先发送
  `1920x1014`，还原时发送 `1024x600`。
- 每次变化都会销毁并重建 display surface；全屏后重建为约 `1920x1088` 的解码 surface，
  还原后重建为 `1024x768`，随后继续收到 IDR/H.264 帧。短暂的 YUV420 解码警告和 stuck
  计数属于切流过渡，未造成持续黑屏。
- 客户端连接参数中的 `resolution adjust` 为 disabled，但窗口变化仍会发送 monitor config；
  因此“自动调分辨率开关”和“全屏按钮通知远端输出”是两个不同路径。
- Wayland 虚拟机端的 `spice-vdagent 0.22.1` 同时报 `Mutter.DisplayConfig without an
  owner`、`VDAgentMonitorsConfig` 尺寸无效；当前会话是 KDE/KWin 而非 GNOME/Mutter，
  这是 `1024x600` 最终回落到 `1024x768` 的主要适配缺口。

## Step 115：KDE Plasma Wayland KScreen 适配与公开仓库

- 新建公开仓库 `pigeon2049/ydyun-vdesktop-linux-driver`，同步 Linux USB/IP、标准 SPICE
  viewer/解析器、客户端分析文档、部署步骤和安全排除范围；不提交官方安装包、运行日志、
  密码、token、auth code 或访问令牌。
- 新增 `wayland/patches/0001-kde-wayland-kscreen.patch`：在上游
  `spice-vdagent 0.22.1` 的用户 agent 中，Wayland 会话改用无 shell 的
  `kscreen-doctor` 查询/设置 KScreen 输出，不增加抢占 virtio agent 端口的第二个守护进程。
- 补丁已在本机完成 C 编译；仓库 54 项 Python 测试、C loader/viewer 构建、Debian 包构建和
  包内容审计通过。包 SHA-256：
  `d488e7a5af65359478b5d4ca47213bff1dce4232551551166354f47398dbd08a`。
- 当前 Wayland 适配优先覆盖单个 QXL 虚拟输出；如果 QXL 没有精确的 `1024x600` DRM 模式，
  会选择最接近的已公布模式。多输出 connector/display-ID 映射和厂商扩展 agent 消息仍列为
  后续工作，未进行猜测性实现。

## Step 116：Wayland 实机补丁最终验证

- 实机首次安装补丁后发现 `kscreen-doctor` 的 Qt 平台选择和 ANSI 颜色控制码会影响解析；
  补丁现在为无 shell 子进程显式设置 `QT_QPA_PLATFORM=wayland`、`TERM=dumb`，并在解析前
  去除 CSI ANSI 序列，同时对查询和设置路径都使用同一解析逻辑。
- 重新编译的 `spice-vdagent` 安装到 Debian trixie x86_64 Plasma Wayland 实机后，用户
  agent 日志已出现 `KScreen current Virtual-1 1920x1080+0+0`，启动过程中不再出现
  `Mutter.DisplayConfig without an owner`；标准显示/输入/音频链路保持原 agent 实现。
- 实机仍会收到官方客户端的未知扩展 agent 消息和 `VDAgentMonitorsConfig` 非标准长度，
  这些属于厂商私有协议边界，补丁明确不猜测、不执行；标准 monitor config 由 KScreen 分支
  处理。最终包 SHA-256：
  `8e1d4dc9ddc33dbfbc92b17eefd6560e77bc6354e358f674699221ca8bff2e58`。

## Step 117：Debian 13 一键教程与 v0.2.51 Release

- 将仓库首页 README 改为从 DD Debian 13 开始的线性教程：确认架构、安装 Debian 通用内核
  和 QXL/DRM、安装 KDE Plasma/Wayland/PipeWire/USB/IP 依赖、下载并校验 Release、安装
  两个 `.deb`、验证画面/分辨率/键鼠/声音和 USB。
- 创建 Git tag `v0.2.51` 并发布公开 Release，资产包括 `ydyun-usbctl_0.2.51-1_amd64.deb`、
  KDE Wayland 版 `spice-vdagent_0.22.1-4.1_amd64.deb` 和 `SHA256SUMS`。
- Release 中的两个 Debian 包均从本地已通过测试的构建产物上传；下载后校验通过，仓库
  工作树保持干净。官方登录、云端认证和私有 USB 控制面仍不被教程伪装成本地标准服务。

## Step 118：目标 Debian 13 实机内核、驱动和 KDE 美化

- 使用普通用户 `zgl` + `sudo` 连接目标机；确认 root SSH 被镜像默认禁用，不再把 root 远程
  登录作为教程前提。Tailscale 地址恢复正常，目标系统为 Debian 13 trixie x86_64。
- 安装并启动 Debian 通用内核 `6.12.107+deb13-amd64`。cloud 内核在运行中不能删除，先通过
  GRUB 一次性启动通用内核，确认 `qxl` 已加载并出现 `/dev/dri/card0` 后，再清理旧 cloud
  内核并重新生成 GRUB；最终 `/boot` 只保留通用内核。
- 目标机安装 `task-kde-desktop`、中文 locale、Noto CJK、Fcitx5、PipeWire、USBIP、QXL/Xorg、
  SDDM，以及 Release `v0.2.51` 的 `spice-vdagent 0.22.1-4.1` 和 `ydyun-usbctl 0.2.51-1`。
  SDDM、qemu-guest-agent、tailscaled active，SPICE agent socket active。
- 按用户提供的 KDE 美化文章落地 Debian 兼容方案：Breeze Dark、Papirus-Dark、Noto Sans/
  Cantarell、24px Breeze 光标、48px 悬浮底部面板；Panel Colorizer v8.0.0 以普通用户安装并
  加入面板，不安装有 Plasma 更新重建风险的 C++ 扩展。
- 当前 `zgl` 的 Plasma Wayland 会话已 active，`kwin_wayland`、`plasmashell`、PipeWire、
  WirePlumber 和用户态 `spice-vdagent` 均在运行。已创建文章中的上下双面板布局：顶部启动器/
  全局菜单/时钟/托盘使用 `ChromeOS` 预设，底部任务栏使用 `Translucent` 预设；QXL、声音、
  输入法和远程 agent 服务仍保持 active。不安装暂缓的 Linux 官方客户端控制面。

## Step 119：v0.2.52 独立补丁版本和更新保护

- 查明旧补丁包与 Debian 官方包均使用 `0.22.1-4.1`，依赖元数据却不同，APT 会把官方构建
  列为同版本更新。构建脚本现固定源码基线，生成 `0.22.1-4.1+ydyun1`，并加入 `kscreen` 依赖。
- 补丁包拥有 `/etc/apt/preferences.d/ydyun-spice-vdagent.pref`：匹配 `+ydyun` 加数字的版本
  优先级为 990，其他版本为 -1；无永久 hold、不影响其他软件包，后续通过 Release 手动更新。
  官方安全修复需要维护者审阅、重应用补丁并发布，规则本身不会自动合并更新。
- 真实 APT 隔离测试覆盖旧包迁移、高版本官方包拦截、下一版补丁升级、其他包正常更新以及
  撤销规则后恢复官方版本。补丁包的 3 项上游测试、控制器的 54 项 Python 测试和构建通过。
- 目标机已从 v0.2.51 升至 `spice-vdagent 0.22.1-4.1+ydyun1`、`ydyun-usbctl 0.2.52-1`；
  文件校验通过，APT 候选为新补丁版，官方包优先级 -1，`apt-get -s upgrade` 显示无升级项。
  重启 guest agent 后，KDE Wayland、PipeWire/WirePlumber 服务仍正常，KScreen 输出 1920×1080。
- Discover 使用的 PackageKit 后端查询也返回无可用更新；新版 agent 日志出现
  `KScreen current Virtual-1 1920x1080+0+0`，确认实际运行补丁代码。
- 新增 `docs/UPDATES.md`，覆盖安装迁移、手动更新、上游安全更新维护责任和恢复官方包步骤。

## Step 120：MTT S3000 vGPU 安装前调查

- SSH 只读确认目标机存在 `1ed5:0222` / subsystem `1ed5:1101` 摩尔线程设备，但没有驱动绑定。
  当前 Plasma Wayland 由 QXL 输出，KWin 实测为 llvmpipe CPU 渲染，无 DRM render 节点。
- 区分 PCI BAR 的 16G 地址窗口和实际显存：根据 1101 命名推测 1 GiB 档位，仍需 GMI/平台确认。
- 查阅官方 S3000 服务器指南、MT vGPU 2.9.2 Guest 指南/兼容说明，以及 MTCapture 文档。
  应取得匹配 Host 的 Linux Guest 包；Debian 13/6.12 未被本次查询的 Guest 支持清单覆盖。
- 新增 `docs/MTT-VGPU.md`，记录证据、获取渠道、安装前检查、回退原则和分层性能验收。
  GPU 渲染与官方客户端抓屏编码分别验证；尚未获取匹配包，没有安装驱动、重启或改变 QXL。

## Step 121：GitHub 摩尔线程驱动检索

- 找到 cl91、elysia-best、dixyes 的 mtgpu 内核代码仓库，并克隆到工作区
  `driver/doc/gpu-research/` 静态检查，未执行第三方安装脚本或操作目标机。
- dixyes 的 `2.7.1-6.12` 分支明确记录 6.12 适配，含 S3000/Guest 定义；这是新增适配参考。
  仓库已归档且依赖预编译核心，不能误称为完整开源 Guest 驱动或现成 Debian 包。
- 三个候选仓库均无 Release；没有找到经过本平台验证的完整 Linux Guest DEB。
  `docs/MTT-VGPU.md` 已补充链接、提交号、依赖缺口和检索边界。

## Step 122：目标机 mtgpu 编译和 Guest 模式实测

- 在目标机普通用户目录构建 dixyes `2.7.1-6.12` / `099f7ea`，原始代码因 Debian
  `6.12.107` 的 `pci_resize_resource` 四参数接口失败；补上参数后生成匹配 vermagic 的模块。
- 构建存在预编译核心 objtool/ENDBR 警告。源码配置 `RGX_NUM_OS_SUPPORTED=1` 只接受 Native；
  未绕过检查、未禁用内核保护，也未让 Native 驱动接管云端 vGPU。
- 实机以 `disable_driver=1` 成功临时加载；加 Guest 参数后内核明确拒绝 `mtgpu_driver_mode=1`。
  这是模块层验证，不是 GPU 硬件加速验证。已卸载实验模块和新增辅助模块。
- 保留 QXL/Wayland 1920×1080 和正常的 SSH/SPICE/PipeWire 服务，没有永久安装、配置自启或重启。
  树外未签名模块留下本次启动 O/E taint 标志；日志和模块已存本地工作区，不发布为驱动成品。
- 文档补充实测结果；后续需要匹配的 Guest 核心和用户态栈，不能只改宏混用 Native 二进制。

## Step 123：下载并审计官方 MUSA SDK 4.0.1 随附驱动

- 使用用户提供的官方限时链接下载 1.7 GiB SDK ZIP，保存在工作区
  `driver/doc/gpu-research/downloads/`；仅提取驱动到 `sdk-4.0.1/`，未执行安装程序。
- 找到完整的 `musa_3.0.0_amd64.deb`，实际包版本
  `2025.03.26-33122-Ubuntu+9f9ebc2c1`，含固件、图形用户态库、GMI 和抓屏编码示例。
- 实际源码配置仍为 Desktop、`RGX_NUM_OS_SUPPORTED=1`，模式检查只接受 Native。
  静态证据说明该包不是所需 Guest 构建；没有为重复验证而向目标机安装或加载。
- 审计维护脚本会触发 DKMS、模块加载、系统配置及 initramfs 更新；因此不盲装此包。
- `docs/MTT-VGPU.md` 第 8 节记录包摘要、关键代码位置、下载来源和未完成事项，
  不保存限时签名或访问凭据，不将此 Native 包作为本项目 Release 发布。

## Step 124：SDK 4.3.0.CC2.1 与 KUAE 2.1.0 下载审计

- 两个用户提供的官方包均下载到工作区，通过 ZIP CRC 检查，记录本地 SHA-256。
- SDK 中确有 `musa_3.3.0-server_amd64.deb`，包版本 `3.3.0-server`，内置文件 MD5 校验通过。
  但 DKMS 配置仍为 `RGX_NUM_OS_SUPPORTED=1`，模式校验只接受 Native，不是 Guest 包。
- KUAE ZIP 提供 Container Toolkit、MTML、sGPU DKMS、GPU Operator；容器文档要求显卡驱动先就绪。
  sGPU 源码依赖已有 mtgpu 节点/符号。Operator 的 vGPU/KubeVirt 功能属于宿主/集群管理，
  驱动仍需从配置来源另外获取，ZIP 未附 Linux Guest 安装包。
- 第 9 节记录证据和区别；未安装、未向目标机部署、未执行镜像同步或集群配置脚本。

## Step 125：Windows GuestOS 最小实现与账户边界

- 基于 `ZTEGuestOS V7.26.20SP2` 和 Windows 驱动备份建立 `windows/` 实现，仅允许安装 ICE
  显示、输入、声音、隧道以及 USB/IP 驱动和服务；闭源二进制不进入 Git 仓库。
- 静态确认厂商 `vdservice/Vdagent.exe` 导入账户创建、密码设置和本地组成员管理 API，并包含
  自动登录逻辑。因此将 `Vdservice/Vdagent`、Credential Provider、AdAs、ZProcessMonitor、
  sysguard、QoE、重定向和遥测组件列入永久拒装清单。
- 剪贴板和自动分辨率改用源码可审计的上游 Windows SPICE agent，安装时要求有效 Authenticode
  签名或调用者固定 SHA-256；不接受厂商同名代理替换。
- 新增 `Install-YdyunGuest.ps1`：按允许列表复制和安装组件，禁用标准远程账户管理入口，清除
  自动登录状态，限制 GuestOS 服务写入 Winlogon，并在安装前后比较本地用户、密码最后修改时间
  和本地组成员。任何变化都立即失败。
- 新增 `Test-AccountBoundary.ps1` 和 `ydyun-guestctl`：源码及 PE 导入表拒绝账户变更 API；控制器
  只提供诊断、允许列表驱动安装及服务启停，不提供用户、密码、组、远程命令或监控功能。
- 新增 `windows/README.md`，记录输入目录、SPICE agent 信任要求、安装、构建、验证和回滚流程。
  Windows 实机加载及官方客户端端到端验收尚待重装后的快照环境执行。

## Step 126：需求改为 Windows 端 100% 开源组件

- 用户明确否决全部原厂二进制。Step 125 中复用 ICE 显示/输入/声音/USB 服务的方案作废，
  `windows/` 已重写为 QXL/VirtIO + 开源 SPICE agent + 可选 usbip-win2，不存在原厂回退路径。
- 备份证据显示旧 Windows 已安装 Red Hat `balloon/vioser/vioinput/vioscsi/viostor/netkvm`，Linux
  实机也能使用 QXL；因此 Proxmox 文档所指向的 Fedora/Red Hat VirtIO-Win ISO 可作为驱动来源。
  先检查 QXL/VirtIO GPU 和 VirtIO serial 硬件 ID，宿主未暴露时直接停止。
- 安装器只接受 `qxldod`、`viogpudo`、`vioser`、`vioinput` 四种 INF，拒绝路径或 INF 内容带
  ZTE/ICE 标记的重签名包。QEMU Guest Agent、balloon service、WebDAV、共享目录、监控和远程
  命令组件均不安装。
- SPICE agent 固定为 `nefarius/vd_agent`，USB/IP 可选 `vadimgrn/usbip-win2`；新增
  `dependencies.lock.json` 固定仓库、提交、许可证和最低 Windows 版本，所有安装包必须固定哈希。
- 账户安全规则继续有效：不创建用户、不改密码、不改组，安装前后快照必须一致，自动登录关闭，
  SPICE agent 被拒绝写入 Winlogon。检测到任何 ICE/ZTE 服务或驱动时拒绝混装。
- 当前 VirtIO-Win 没有 Windows virtio-snd 驱动。诊断工具要求宿主暴露 Windows inbox 可驱动的
  HDA/AC97 声卡；若缺失必须由宿主补设备，不能使用原厂虚拟声卡。
- `ydyun-guestctl.exe` 已在 Debian 13 用 MinGW x86_64 严格告警交叉编译通过，导入表仅包含
  ADVAPI32、KERNEL32、MSVCRT、SETUPAPI，不含账户/组/密码 API。Windows 实机验收仍待执行。
- 已把四个锁定提交克隆到主仓库外的 `driver/doc/reference/windows-open-source/`，扫描账户、密码、
  本地组和自动登录相关 API/命令，四个源码树均为零匹配；新增
  `docs/WINDOWS-OPEN-SOURCE-AUDIT.md` 保存证据、排除项和剩余实机验收清单。
- 使用 PowerShell 7.6.6 parser 对两个 `.ps1` 完成语法解析，`Test-AccountBoundary.ps1` 源码边界
  测试通过；依赖锁 JSON、Git diff whitespace 检查和 MinGW/CMake 双路径构建均通过。
- 新增 `Build-OpenSourcePackage.ps1` 和 `Install-YdyunOpenGuest.ps1`：可从 VirtIO-Win ISO、固定
  哈希的开源 SPICE agent（以及可选 usbip-win2）生成带 `manifest.json`、`SHA256SUMS` 和诊断工具
  的单一 ZIP 包；安装时不联网、不包含原厂文件、不覆盖既有输出目录。

## Step 127：生成离线 Windows 救援安装包

- 下载并校验 Fedora 镜像中的 `virtio-win-0.1.302.iso`，只抽取 `qxldod`、`viogpudo`、`vioinput`
  和 `vioser` 的 Windows 10 x86_64 payload；扫描确认 payload 中无 ZTE/ICE 标记。
- 下载 `nefarius/vd_agent` `v0.13.0` x64 MSI，SHA-256 为
  `01ece947b86b83c7f777ef9548b4bc61cfd783d0bd2ed99160cdee77686b62de`。
- 生成离线包 `dist/ydyun-windows-open-source-w10.zip`，大小约 5.2 MiB；根目录仅有一个 BAT
  和一个 `resources` 目录，包内包含开源显示、
  鼠标键盘、VirtIO serial 驱动、SPICE agent、诊断工具、PowerShell 安装脚本、键盘可操作的
  `.bat` 安装入口和校验清单。
- ZIP SHA-256：`28173be72236be9524d898f94d0ca8f1f9f3bd9a8ea57efaf91c2a8ac7ab1f9a`。
- 复核时发现 Windows PowerShell 5.1 对无 BOM UTF-8 脚本兼容性不足；已把包内两个 `.ps1` 转为
  UTF-8 BOM + CRLF，并重新通过 PowerShell parser 和哈希校验。
- 实机验证发现 `RegistryRights` 命名空间错误，已改为
  `System.Security.AccessControl.RegistryRights`，重新通过 parser、包校验并更新发布 ZIP。
- 已通过 ZIP 完整性、manifest 哈希、`SHA256SUMS`、PowerShell 语法、账户边界检查和 MinGW
  x86_64 交叉编译验证。该包不包含 USB/IP 安装器；USB 优先依赖宿主官方 SPICE/USB 重定向。

## Step 128：允许离线包在设备尚未出现时完成安装

- Windows 实机返回 `pnputil /add-driver ... /install` 退出码 259，含义是当前没有匹配的设备，
  不是 INF 损坏。安装器已改为先把每个允许的 INF 加入 Driver Store，再尝试绑定；259 只记为
  非致命结果，安装继续执行。
- 硬件探测从硬失败改为提示：会继续处理 `qxldod`、`viogpudo`、`vioinput`、`vioser`，但画面、
  鼠标键盘和 SPICE 剪贴板能否工作仍取决于宿主是否暴露对应 QXL/VirtIO 设备。
- `windows/README.md` 已补充 GPU 安装教程：离线包内含两套 GPU 驱动，QXL 使用 `qxldod.inf`，
  VirtIO GPU 使用 `viogpudo.inf`，重启后由 Windows 按 PCI 设备选择绑定。
- 重新生成离线发布包：`release/ydyun-windows-open-source-w10.zip`，SHA-256 为
  `c97c0d75debdafc07b38b92afe8513e66fc3571a07e4391cb869888f15a3abd2`；包内 33 个文件，
  无需联网下载驱动。

## Step 129：修复 PowerShell 入口的未初始化退出码

- Windows PowerShell 5.1 在子脚本成功返回且没有执行外部命令时，`$LASTEXITCODE` 可能不存在；
  入口脚本在严格模式下直接读取它会在安装成功后误报失败。
- `Install-YdyunOpenGuest.ps1` 现在通过 `Get-Variable -ErrorAction SilentlyContinue` 读取退出码，
  未定义时按 0 处理，不再出现“检索不到变量 `$LASTEXITCODE`”。
- 已通过 PowerShell parser、账户边界测试、CMake 构建和 ZIP 完整性检查；最新包 SHA-256 为
  `db4235076f522467f8f52971b8b2c4b32054e70239ee0e9b8186ac852cf93246`。
