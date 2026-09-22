# YDYUN vDesktop Linux Driver

这是一个面向 Debian trixie x86_64 的云电脑 Linux 适配工程。目标是保留云桌面
必要的数据面：画面、鼠标、键盘、声音，以及 USB 存储和 USB HID 转发；不把官方
Windows/Linux 客户端中的安全策略、QoE、监控、trace、进程守护和厂商 root 策略
服务带入 Linux 运行时。

当前仓库分成两条边界：

- `linux/`：标准 USB/IP/VHCI 控制器、标准 SPICE 解析器和可选的标准 SPICE viewer。
- `wayland/`：针对 KDE Plasma Wayland 的 `spice-vdagent` KScreen 适配补丁。
- `docs/`：官方客户端逆向验证、协议边界、部署记录和复现实验。

## 已验证范围

- Debian 13 trixie、x86_64、QXL DRM、KDE Plasma、PipeWire。
- 官方客户端全屏/还原会发送 SPICE monitor config；Wayland 原版
  `spice-vdagent` 会错误尝试 Mutter DBus，导致 KDE 最终回落到其他已公布模式。
- 本仓库补丁让 Plasma Wayland 使用 `kscreen-doctor` 选择 QXL/KScreen 已公布的最接近
  模式；客户端、SPICE agent 传输、键鼠和声音路径不被替换。
- `dummy_hcd -> usbip-host -> usbipd -> vhci-hcd` 的虚拟复合键盘、鼠标、存储回归。
- 标准 SPICE display/input viewer 和受限离线协议解析器。

厂商登录、JWAE/SCG、私有 USB 会话前置和云端认证不在清洁实现中自动猜测。需要真实
云会话时，应由操作者先用合法官方流程取得授权 endpoint，再把它交给明确的适配边界。

## 快速开始

```sh
sudo apt update
sudo apt install build-essential debhelper-compat python3 usbip \
  pkg-config libspice-client-gtk-3.0-dev spice-vdagent kscreen \
  kde-plasma-desktop qemu-guest-agent

git clone https://github.com/pigeon2049/ydyun-vdesktop-linux-driver.git
cd ydyun-vdesktop-linux-driver

python3 -m unittest discover -s linux/tests -v
dpkg-buildpackage -us -uc -b
```

安装生成的 Debian 包前先确认目标设备不是系统唯一的实体键盘、鼠标、Hub 或根 Hub。
仓库的 systemd 单元默认不启用：

```sh
sudo dpkg -i ../ydyun-usbctl_*_amd64.deb
sudo ydyun-usbctl doctor
systemctl is-enabled ydyun-usbctl.service || true
```

## KDE Plasma Wayland 分辨率适配

这部分修改的是 Debian `spice-vdagent`，不是另起一个进程去抢占
`/dev/virtio-ports/com.redhat.spice.0`。补丁位于：

```text
wayland/patches/0001-kde-wayland-kscreen.patch
```

自动取得 Debian 源码、应用补丁并编译：

```sh
./wayland/scripts/build-patched-spice-vdagent.sh
```

脚本只在仓库的 `build/` 下工作，不使用 `/tmp`，不会自动安装或启动服务。完成后按
`docs/WAYLAND-RESOLUTION.md` 安装生成包、重启用户 agent，并执行全屏/还原回归。

关键验证：

```sh
echo "$XDG_SESSION_TYPE"       # 应为 wayland
kscreen-doctor -o
journalctl --user -u spice-vdagent --since today --no-pager
```

## USB 转发

标准 USB/IP 使用 Linux 内核原生驱动：

```sh
sudo modprobe usbip-core vhci-hcd
sudo ydyun-usbctl list
sudo ydyun-usbctl attach BUSID
sudo ydyun-usbctl port
```

USB 存储由 `usb-storage`/`uas` 接管，键盘和鼠标由 `usbhid` 接管。物理设备上行导出
必须显式写入配置并执行 `export`；默认不会自动接管实体设备。Hub、root Hub 和未列入
配置的设备会被拒绝。

## 安全范围

默认包不包含官方 `usbredirect`、安全库、QoE/trace 库、监控脚本、WinDivert、SysGuard
或 Windows `.sys` 驱动。私有协议工具只做离线、只读、长度受限解析；不会从文档样例
恢复密码、token 或 auth code，也不会通过裸端口绕过云端认证。

详情从 [Wayland 分辨率适配](docs/WAYLAND-RESOLUTION.md)、[USB 适配说明](linux/README.md)
和 [客户端分析](docs/CLIENT-ANALYSIS.md) 开始。
