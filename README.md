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

Windows GuestOS 的 100% 开源实现、账户安全边界和安装步骤见
[Windows 开源 GuestOS 实现](windows/README.md)。它完全拒绝 ZTE/ICE 原厂二进制，使用
QXL/VirtIO、开源 SPICE agent 和可选 usbip-win2；任何新增用户、改密码、修改管理员组或启用
自动登录的行为都会使安装失败。

Windows 端不是把大体积 ISO/MSI 直接提交进仓库；在 Windows 构建机执行
`windows/Build-OpenSourcePackage.ps1` 会生成唯一的
`dist\ydyun-windows-open-source.zip`，目标机只需运行包内的
`windows\Install-YdyunOpenGuest.ps1`。

## 最快安装方式：从 DD Debian 13 到完成

以下命令适用于刚刚 DD 完成、可以 SSH 登录的 Debian 13 x86_64 云电脑。DD 完成后的系统
最初只有 SSH/命令行，没有 KDE 图形画面，这是正常现象；必须先通过 SSH 安装内核、KDE
和本项目的两个 `.deb`，重启后官方云电脑窗口才会显示桌面。不要假设 root 可以 SSH：很多
DD 镜像默认禁止 root 远程登录。请使用镜像创建的普通用户登录，再执行 `sudo -i`；后文的
系统级命令均假设已经进入 root shell。

### 0. 重装完成后优先设置 Tailscale

Tailscale 的安装状态保存在当前系统盘中，执行 DD 后不会保留。因此它不能保证 DD 写盘
过程中不断线；DD 期间请保留云厂商官方控制台、VNC 或当前可达的管理网络作为救援入口。
新 Debian 第一次恢复 SSH 后，先安装并登录 Tailscale，后续安装 KDE 和驱动优先使用
Tailscale 地址操作：

```sh
curl -fsSL https://tailscale.com/install.sh | sh
systemctl enable --now tailscaled
tailscale up
```

`tailscale up` 会输出登录链接，请在浏览器完成授权。授权完成后记录 Tailscale 地址：

```sh
tailscale ip -4
tailscale status
```

之后使用普通用户连接，替换为 `tailscale ip -4` 输出的地址；如果当前环境没有公网，
仍然使用云厂商内网、控制台或其他已连通 Tailscale 的机器访问：

```sh
ssh <普通用户名>@100.x.y.z
sudo -i
```

不要使用 `ssh root@...` 作为默认方案；如果镜像只允许控制台登录 root，先在控制台创建或
启用一个具备 sudo 权限的普通用户，再从 SSH 继续。

如果 `tun` 模块不存在，先执行：

```sh
modprobe tun
printf 'tun\n' >/etc/modules-load.d/tun.conf
systemctl restart tailscaled
```

官方安装方式和 Debian 支持范围见 [Tailscale Linux 安装文档](https://tailscale.com/docs/install/linux)。

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

执行 `reboot` 后 SSH 会断开。等待约 1～3 分钟，再使用官方控制台或当前可达管理网络重新连接；
确认已进入新内核，并且 QXL 和 DRM 设备存在：

```sh
uname -r
modprobe qxl
ls -l /dev/dri/card0
```

如果 `uname -r` 仍然以 `-cloud-amd64` 结尾，在 GRUB 的 `Advanced options for Debian`
中先选择不带 `-cloud` 的通用内核启动；确认 `uname -r` 已变成 `+deb13-amd64`、`/dev/dri/card0`
已存在后，再清理旧 cloud 内核，避免 GRUB 每次默认回到不含 QXL 的内核：

```sh
cloud_pkgs="$(dpkg-query -W -f='${db:Status-Status} ${binary:Package}\n' \
  'linux-image-*cloud*' 2>/dev/null | awk '$1 == "installed" {print $2}')"
if [ -n "$cloud_pkgs" ]; then
  apt purge -y $cloud_pkgs
  update-grub
fi
```

不要在仍运行 cloud 内核时直接删除它；内核包会拒绝该操作。通用内核启动成功后再清理。

如果 `modprobe qxl` 报模块不存在，先检查当前内核：

```sh
find "/lib/modules/$(uname -r)" -name 'qxl.ko*'
```

当前 Release 的实机验证内核为 Debian 13 通用 `6.12.107+deb13-amd64`。

### 3. 单独创建 KDE 普通用户（必须执行）

不要用 root 登录或启动 KDE Plasma。请先单独执行下面的命令创建普通桌面用户；不要把这一段
和后面的 KDE 安装命令合并复制：

```sh
adduser cloud
usermod -aG audio,video,render cloud
```

`adduser cloud` 会交互式要求设置密码和基本信息。`cloud` 只是示例用户名，如果已有普通
用户，跳过 `adduser`，将下面命令中的 `cloud` 替换为已有用户名即可。不要把 KDE 用户加入
`input` 或 `root` 组。

### 4. 通过 SSH 安装 KDE Plasma、中文环境和基础转发依赖

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

systemctl set-default graphical.target
systemctl enable sddm qemu-guest-agent
reboot
```

这一步必须通过 SSH 执行；在安装和重启前，官方客户端仍然可能只显示命令行。这里使用
Debian 的 `task-kde-desktop` 安装完整 KDE Plasma 桌面，使用 `task-chinese-s`
和 Noto CJK 字体提供简体中文界面与中文字形，使用 Fcitx5 + 拼音插件提供中文输入。

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

### 5. 参考 KDE 美化方案（可选）

本项目参考了[我的 KDE Plasma 6 美化方案分享](https://blog.sotkg.com/2025/08/kde-customization)
的上下布局、悬浮面板、透明效果和深色配色思路。Debian 没有 Fedora/Moe 全局主题，因此目标
机采用兼容性更好的 Breeze Dark + Papirus-Dark + Noto Sans/Cantarell 等价组合，不修改 QXL、
Wayland、PipeWire 或输入法链路：

```sh
# 系统级安装，仍在 sudo -i 的 root shell 中执行
apt install -y papirus-icon-theme fonts-cantarell plasma-widgets-addons
```

然后以普通 KDE 用户执行：

```sh
kwriteconfig6 --file kdeglobals --group General --key ColorScheme BreezeDark
kwriteconfig6 --file kdeglobals --group General --key font \
  'Noto Sans,10,-1,5,50,0,0,0,0,0'
kwriteconfig6 --file kdeglobals --group KDE --key LookAndFeelPackage org.kde.breezedark.desktop
kwriteconfig6 --file kdeglobals --group Icons --key Theme Papirus-Dark
kwriteconfig6 --file kdeglobals --group KDE --key widgetStyle Breeze
kwriteconfig6 --file kcminputrc --group Mouse --key cursorTheme breeze_cursors
kwriteconfig6 --file kcminputrc --group Mouse --key cursorSize 24
kwriteconfig6 --file plasmashellrc --group PlasmaViews --group 'Panel 2' \
  --group Defaults --key thickness 48
```

Panel Colorizer 可以从 KDE 商店安装；也可以下载 Plasma 6 的 `.plasmoid` 包后由普通用户
安装。目标机已安装 v8.0.0 并加入底部面板：

```sh
kpackagetool6 --type Plasma/Applet --install plasmoid-panel-colorizer-v8.0.0.plasmoid
```

登录桌面后右键面板 → Panel colorizer → 预设，底部面板建议选择 `Translucent` 或 `Dock`，
上方面板如需新增可选择 `ChromeOS`。这里不安装 Panel Colorizer 的 C++ 扩展；该扩展可能在
Plasma 更新后需要重新编译，远程桌面优先保持可恢复性。插件的运行时依赖和更新注意事项见
[Panel Colorizer 安装说明](https://github.com/luisbocanegra/plasma-panel-colorizer#installation)。

### 6. 下载 Release 中已经编译好的 Debian 包

不要在目标云电脑上重新编译。直接下载本项目的 Release 包：

```sh
mkdir -p ~/ydyun-release
cd ~/ydyun-release

curl -fLO https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/releases/download/v0.2.52/ydyun-usbctl_0.2.52-1_amd64.deb
curl -fLO https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/releases/download/v0.2.52/spice-vdagent_0.22.1-4.1+ydyun1_amd64.deb
curl -fLO https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/releases/download/v0.2.52/SHA256SUMS
sha256sum -c SHA256SUMS
```

旧版用户请先阅读[升级与更新策略](docs/UPDATES.md)。v0.2.52 起的补丁包有独立 `+ydyun1`
版本号，并自带 APT 规则防止官方包覆盖 KScreen 补丁；普通系统更新不受影响。

`spice-vdagent_0.22.1-4.1+ydyun1_amd64.deb` 是本项目针对 KDE Plasma Wayland 编译的版本；
它不是另外抢占 virtio 通道的守护进程，而是对 Debian `spice-vdagent` 的显示模式处理
做了 KScreen 适配。

### 7. 通过 SSH 安装 Release 包，完成后才有图形画面

仍然在 SSH 终端中执行：

```sh
apt install -y ./spice-vdagent_0.22.1-4.1+ydyun1_amd64.deb ./ydyun-usbctl_0.2.52-1_amd64.deb
systemctl restart spice-vdagentd
```

如果此前设置过 `apt-mark hold spice-vdagent`，安装前先执行 `apt-mark unhold spice-vdagent`。
后续补丁从 GitHub Releases 手动安装；上游安全更新需要合并补丁后重新发布。
安装后可用 `apt-cache policy spice-vdagent` 检查：官方包优先级应为 `-1`，候选版应带 `+ydyun`。

安装完成后重启：

```sh
reboot
```

等待 SSH 恢复后，官方云电脑客户端才应出现 KDE 图形桌面。第一次进入 KDE 后，再在桌面
终端中执行下面的用户级命令；不要在 root SSH 会话中执行 `systemctl --user`：

```sh
systemctl --user restart spice-vdagent.service
```

确认服务状态：

```sh
pgrep -a spice-vdagent
systemctl is-active spice-vdagentd.socket
systemctl --user is-active spice-vdagent.service
```

### 8. 验证画面、分辨率、鼠标、键盘和声音

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

### 9. 验证 USB 转发

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

本次 `v0.2.52` 已在 Debian 13 x86_64 上完成 54 项 Python 测试、C 构建、Debian 打包、
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
- `windows/`：100% 开源的 Windows GuestOS 安装器、控制程序和账户边界审计。
- `docs/`：客户端行为、画面转发、USB 协议、分辨率和每一步进度记录。

详细资料：

- [Wayland 分辨率适配](docs/WAYLAND-RESOLUTION.md)
- [USB 适配说明](linux/README.md)
- [客户端行为分析](docs/CLIENT-ANALYSIS.md)
- [安全范围](docs/SECURITY-SCOPE.md)
- [Windows 开源 GuestOS 实现](windows/README.md)
- [Windows 开源组件审计](docs/WINDOWS-OPEN-SOURCE-AUDIT.md)
- [总体进度](docs/PROGRESS.md)
