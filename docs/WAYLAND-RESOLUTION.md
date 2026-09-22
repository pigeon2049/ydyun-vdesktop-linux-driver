# KDE Plasma Wayland 分辨率适配与验证

## 1. 为什么要改 `spice-vdagent`

官方客户端的全屏/还原按钮会在 SPICE 主通道发送 monitor config。实测还原时客户端
发送 `1024x600`，全屏时发送 `1920x1080`，并在尺寸变化后销毁/重建 display surface。

Debian 13 的原版 `spice-vdagent 0.22.1` 在 Plasma Wayland 中会出现：

```text
failed to call GetCurrentState from mutter over DBUS
org.gnome.Mutter.DisplayConfig without an owner
invalid message size for VDAgentMonitorsConfig
```

第一条是确定的桌面后端不匹配：当前会话是 KDE/KWin，不拥有 Mutter 的 DBus 名称。
第二条还可能表示官方客户端使用了扩展过的 agent 消息；因此本补丁只处理标准
`VDAgentMonitorsConfig`，不会猜测私有消息长度。

## 2. 构建补丁版 agent

```sh
sudo apt update
sudo apt install build-essential devscripts debhelper-compat \
  libasound2-dev libdbus-1-dev libdrm-dev libgtk-3-dev \
  libpciaccess-dev libspice-protocol-dev libsystemd-dev \
  libx11-dev libxfixes-dev libxinerama-dev libxrandr-dev pkg-config

./wayland/scripts/build-patched-spice-vdagent.sh
```

脚本会把源码和构建产物放在 `build/`，输出的 `.deb` 在仓库父目录。也可以手动执行：

```sh
mkdir -p build
cd build
apt-get source spice-vdagent
cd spice-vdagent-0.22.1
patch -p1 < ../../wayland/patches/0001-kde-wayland-kscreen.patch
dpkg-buildpackage -us -uc -b
```

## 3. 安装到 Debian 云电脑

先保存当前包版本，安装后可用同一个版本源恢复：

```sh
dpkg-query -W -f='${Package} ${Version}\n' spice-vdagent
sudo dpkg -i ../spice-vdagent_*_amd64.deb
sudo systemctl restart spice-vdagentd
```

用户会话中的 `spice-vdagent` 通常由 `/etc/xdg/autostart/spice-vdagent.desktop` 启动。
因此还需要注销并重新登录 Plasma，或结束用户 agent 后让桌面重新启动它：

```sh
pkill -u "$USER" -x spice-vdagent || true
```

不要在官方云电脑正在传输重要数据时强制结束 agent；显示会短暂重连，但不会删除桌面
文件。

## 4. 回归测试

确认环境：

```sh
echo "$XDG_SESSION_TYPE"
echo "$WAYLAND_DISPLAY"
command -v kscreen-doctor
kscreen-doctor -o
pgrep -a spice-vdagent
```

然后在官方客户端依次点击：还原窗口、全屏、再次还原。并在云电脑端观察：

```sh
journalctl --user -u spice-vdagent --since '5 minutes ago' --no-pager
journalctl -u spice-vdagentd --since '5 minutes ago' --no-pager
kscreen-doctor -o
```

成功标准：

- agent 日志出现 KScreen 设置的目标模式；
- 全屏后 QXL/KScreen 几何为客户端全屏尺寸；
- 还原后使用 QXL 已公布的最接近模式，不再调用 Mutter DBus；
- 画面能继续收到 IDR/增量帧，鼠标、键盘、声音不因模式变化失效。

## 5. 已知限制

- `1024x600` 不一定是 QXL/DRM 的已公布模式；本补丁会选择最接近的模式，例如
  `1024x768`。要做到像素级 `1024x600`，需要在 QXL/DRM 或虚拟机显示设备层增加该模式，
  不是 KScreen 用户态单独能够保证的。
- 当前补丁优先处理单个连接的虚拟输出。多显示器需要继续实现 connector 与 SPICE
  display ID 的双向映射。
- 官方客户端的扩展 agent 消息仍由上游代理拒绝；扩展协议需要有合法、完整的版本化
  样本后再单独增加解析器。
