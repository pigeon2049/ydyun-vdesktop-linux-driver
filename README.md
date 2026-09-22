# YDYUN vDesktop Linux Driver

这是一个面向 Debian 13 trixie、x86_64 云电脑的安装教程和适配代码。

目标是让 Linux 虚拟机能够通过官方云电脑会话正常使用：

- 画面显示
- 鼠标和键盘输入
- 声音播放
- USB 存储、USB 键盘和 USB 鼠标转发
- KDE Plasma Wayland 下的全屏/还原分辨率调整

本项目不打包 Windows `.sys` 驱动，也不引入官方客户端中的安全策略、监控、QoE、trace、
进程守护和厂商 root 策略服务。官方登录和云端认证仍由合法的官方控制面完成。

## 最快安装方式：从 DD Debian 13 到完成

以下命令适用于刚刚 DD 完成、可以 SSH 登录的 Debian 13 x86_64 云电脑。建议使用 root
账号执行系统安装步骤。

### 1. 确认系统版本和架构

```sh
cat /etc/debian_version
dpkg --print-architecture
```

应分别看到 Debian 13 系列版本和 `amd64`。如果不是 Debian 13 x86_64，请不要直接安装本
Release 的 `.deb`。

### 2. 换成包含 QXL/DRM 的 Debian 通用内核

部分云电脑镜像使用 `-cloud-amd64` 内核，该内核可能没有 QXL 模块，结果是 SDDM 已启动但
官方客户端只能看到纯命令行。安装 Debian 通用内核：

```sh
apt update
apt install -y linux-image-amd64 linux-headers-amd64 firmware-linux-free
reboot
```

重启后确认已进入新内核，并且 QXL 和 DRM 设备存在：

```sh
uname -r
modprobe qxl
ls -l /dev/dri/card0
```

如果 `modprobe qxl` 报模块不存在，先检查当前内核：

```sh
find "/lib/modules/$(uname -r)" -name 'qxl.ko*'
```

当前 Release 的实机验证内核为 Debian 13 通用 `6.12.107+deb13-amd64`。

### 3. 安装 KDE Plasma、音频和基础转发依赖

```sh
apt update
apt install -y \
  task-kde-desktop task-chinese-s sddm \
  locales fonts-noto-cjk fonts-noto-cjk-extra \
  pipewire pipewire-pulse wireplumber \
  qemu-guest-agent kscreen \
  usbip python3 curl ca-certificates \
  fcitx5 fcitx5-chinese-addons fcitx5-frontend-all \
  kde-config-fcitx5 im-config

# 生成简体中文 locale，并设置系统默认语言。
sed -i 's/^# *zh_CN.UTF-8 UTF-8/zh_CN.UTF-8 UTF-8/' /etc/locale.gen
locale-gen
update-locale LANG=zh_CN.UTF-8 LANGUAGE=zh_CN:zh:en_US:en LC_CTYPE=zh_CN.UTF-8
timedatectl set-timezone Asia/Shanghai

# KDE 必须使用普通用户登录，不要用 root 启动 Plasma。
# adduser 会交互式要求设置该用户密码和基本信息。
adduser cloud
usermod -aG audio,video,render cloud

systemctl set-default graphical.target
systemctl enable sddm qemu-guest-agent
reboot
```

这里使用 Debian 的 `task-kde-desktop` 安装完整 KDE Plasma 桌面，使用 `task-chinese-s`
和 Noto CJK 字体提供简体中文界面与中文字形，使用 Fcitx5 + 拼音插件提供中文输入。
`cloud` 是示例普通桌面用户；如果已经存在普通用户，不要重复执行 `adduser`，将下面命令
中的 `cloud` 替换为现有用户名即可。不要把 KDE 用户加入 `input` 或 `root` 组。

重启后，在登录界面选择普通用户 `cloud`，再选择会话 `Plasma (Wayland)`。不要选择 root
登录 KDE。首次进入 KDE 后，以普通桌面用户执行下面的命令，把当前用户界面切换为简体中文，
并设置 Fcitx5：

```sh
kwriteconfig6 --file plasma-localerc --group Formats \
  --key LANG zh_CN.UTF-8
kwriteconfig6 --file plasma-localerc --group Translations \
  --key LANGUAGE zh_CN
im-config -n fcitx5
```

注销 KDE 并重新登录后语言设置生效。也可以打开“系统设置 → 区域和语言”，确认首选语言
为“简体中文”，键盘输入法中添加“拼音”。

登录后确认桌面、语言和 Wayland：

```sh
echo "$XDG_SESSION_TYPE"       # 应为 wayland
echo "$WAYLAND_DISPLAY"        # 通常为 wayland-0
locale | sed -n '1,8p'          # LANG 应为 zh_CN.UTF-8
kscreen-doctor -o
```

如果暂时选择 Plasma X11，USB 和标准 SPICE 功能仍可验证；Wayland 分辨率补丁只有在
`XDG_SESSION_TYPE=wayland` 时生效。

### 4. 下载 Release 中已经编译好的 Debian 包

不要在目标云电脑上重新编译。直接下载本项目的 Release 包：

```sh
mkdir -p ~/ydyun-release
cd ~/ydyun-release

curl -fLO https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/releases/download/v0.2.51/ydyun-usbctl_0.2.51-1_amd64.deb
curl -fLO https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/releases/download/v0.2.51/spice-vdagent_0.22.1-4.1_amd64.deb
curl -fLO https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/releases/download/v0.2.51/SHA256SUMS
sha256sum -c SHA256SUMS
```

`spice-vdagent_0.22.1-4.1_amd64.deb` 是本项目针对 KDE Plasma Wayland 编译的版本；
它不是另外抢占 virtio 通道的守护进程，而是对 Debian `spice-vdagent` 的显示模式处理
做了 KScreen 适配。

### 5. 安装 Release 包

```sh
apt install -y ./spice-vdagent_0.22.1-4.1_amd64.deb ./ydyun-usbctl_0.2.51-1_amd64.deb
systemctl restart spice-vdagentd
```

然后注销 KDE，再重新登录一次。也可以在已经登录的 KDE 终端中执行：

```sh
systemctl --user restart spice-vdagent.service
```

确认服务状态：

```sh
pgrep -a spice-vdagent
systemctl is-active spice-vdagentd
systemctl --user is-active spice-vdagent.service
```

### 6. 验证画面、分辨率、鼠标、键盘和声音

先看 Wayland 输出和 agent 日志：

```sh
kscreen-doctor -o
journalctl --user -u spice-vdagent --since '10 minutes ago' --no-pager
journalctl -u spice-vdagentd --since '10 minutes ago' --no-pager
```

成功时应能看到类似：

```text
KScreen current Virtual-1 1920x1080+0+0
```

随后在官方云电脑窗口中依次点击：还原窗口、全屏、再次还原。预期结果：

1. 画面短暂重建后继续显示，不持续黑屏。
2. 全屏进入 QXL/KScreen 已公布的 `1920x1080` 模式。
3. 还原请求如果是 `1024x600`，而 QXL 没有这个 DRM 模式，则选择最接近的已公布模式，
   通常是 `1024x768`。
4. 鼠标、键盘和声音不因切换分辨率失效。

### 7. 验证 USB 转发

```sh
modprobe usbip-core vhci-hcd
ydyun-usbctl doctor
ydyun-usbctl list
ydyun-usbctl port
```

`doctor` 只做本地只读检查，不会连接云端、attach 或接管真实设备。

当官方控制面已经提供一个获得授权的 USB/IP endpoint 后，编辑配置：

```sh
sudoedit /etc/ydyun-usb/ydyun-usb.conf
```

填入实际会话提供的地址，不要把示例地址当成固定云端地址：

```ini
[connection]
remote_host = <authorized-usb-endpoint>
remote_port = 3240

[devices]
# 由远端 list 输出确认后再填写
# 1-2 = USB storage
# 1-3 = keyboard
# 1-4 = mouse
```

然后执行：

```sh
ydyun-usbctl list
ydyun-usbctl attach BUSID
ydyun-usbctl port
```

USB 存储由 Linux `usb-storage`/`uas` 驱动接管，键盘鼠标由 `usbhid` 接管。默认不自动
接管实体设备，也不会导出系统根 Hub、蓝牙控制器或未列入配置的设备。

## Release 内容

每个 Release 至少包含以下文件：

- `ydyun-usbctl_<version>_amd64.deb`：USB/IP/VHCI 控制器、标准 SPICE 工具和诊断命令。
- `spice-vdagent_<version>_amd64.deb`：KDE Plasma Wayland KScreen 适配版。
- `SHA256SUMS`：下载后用于校验完整性。

本次 `v0.2.51` 已在 Debian 13 x86_64 上完成 54 项 Python 测试、C 构建、Debian 打包、
包内容审计和 Wayland 实机启动验证。

## 从源码构建

如果需要自行构建而不是使用 Release：

```sh
apt install -y build-essential debhelper-compat pkg-config \
  libspice-client-gtk-3.0-dev python3 usbip

git clone https://github.com/pigeon2049/ydyun-vdesktop-linux-driver.git
cd ydyun-vdesktop-linux-driver
python3 -m unittest discover -s linux/tests -v
make -C linux check
dpkg-buildpackage -us -uc -b
```

构建 KDE Wayland agent：

```sh
apt install -y devscripts libasound2-dev libdbus-1-dev libdrm-dev libgtk-3-dev \
  libpciaccess-dev libspice-protocol-dev libsystemd-dev \
  libx11-dev libxfixes-dev libxinerama-dev libxrandr-dev

./wayland/scripts/build-patched-spice-vdagent.sh
```

构建脚本和测试默认使用仓库的 `build/` 目录，不使用 `/tmp`。

## 目录说明

- `linux/`：USB/IP、VHCI、标准 SPICE viewer、协议探针和测试。
- `wayland/`：KDE Plasma Wayland 的 `spice-vdagent` KScreen 补丁和构建脚本。
- `docs/`：客户端行为、画面转发、USB 协议、分辨率和每一步进度记录。

详细资料：

- [Wayland 分辨率适配](docs/WAYLAND-RESOLUTION.md)
- [USB 适配说明](linux/README.md)
- [客户端行为分析](docs/CLIENT-ANALYSIS.md)
- [安全范围](docs/SECURITY-SCOPE.md)
- [总体进度](docs/PROGRESS.md)
