# 补丁包版本与更新策略

## v0.2.52 修复了什么

旧 Release v0.2.51 的补丁包沿用了 Debian 官方版本 `0.22.1-4.1`，但编译后的依赖元数据不同。
APT 因而可能把同版本官方构建列为更新，并覆盖 KDE Wayland 的 KScreen 补丁。

从 v0.2.52 开始，补丁包版本为 `0.22.1-4.1+ydyun1`，控制器为 `ydyun-usbctl 0.2.52-1`。
Debian 版本比较会把前者排在 `0.22.1-4.1` 之后；构建脚本固定原始源码版本，并添加 `kscreen`
运行时依赖。再次执行构建脚本不会重复添加版本后缀或 changelog。

## 自动安装的保护规则

补丁包拥有配置文件 `/etc/apt/preferences.d/ydyun-spice-vdagent.pref`：

```text
Package: spice-vdagent
Pin: version /[+]ydyun[0-9]+$/
Pin-Priority: 990

Package: spice-vdagent
Pin: version *
Pin-Priority: -1
```

这只影响 `spice-vdagent`：允许本项目后续补丁版本，拒绝没有 `+ydyun` 后缀的候选版本，
包括未来版本号更高的官方包。其他 Debian 软件包继续正常升级。不设置永久 `apt-mark hold`，
也不使用会强制降级的优先级。版本后缀是选择规则，不是签名或可信来源证明，请只安装本项目
Release 中校验过的包。[APT 规则说明](https://manpages.debian.org/trixie/apt/apt_preferences.5.en.html)

本项目目前通过 GitHub Releases 分发，**没有自动更新 APT 仓库**；此规则不会替你下载新补丁。
官方 `spice-vdagent` 的功能和安全更新也会被拦住，需审阅更新、重新应用并验证补丁后发布新的
下游包，不能把长期停留旧版本当作维护策略。本次仅修复打包与更新策略，未更新上游程序版本。

## 从 v0.2.51 升级（普通用户 SSH 登录后执行）

```sh
mkdir -p ~/ydyun-release-v0.2.52
cd ~/ydyun-release-v0.2.52
curl -fL --connect-timeout 15 --max-time 180 -O https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/releases/download/v0.2.52/spice-vdagent_0.22.1-4.1+ydyun1_amd64.deb
curl -fL --connect-timeout 15 --max-time 180 -O https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/releases/download/v0.2.52/ydyun-usbctl_0.2.52-1_amd64.deb
curl -fL --connect-timeout 15 --max-time 180 -O https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/releases/download/v0.2.52/SHA256SUMS
sha256sum -c SHA256SUMS
# 如果此前按旧建议设置过 hold，先取消它。
sudo apt-mark unhold spice-vdagent
sudo apt install -y ./spice-vdagent_0.22.1-4.1+ydyun1_amd64.deb ./ydyun-usbctl_0.2.52-1_amd64.deb
sudo systemctl restart spice-vdagentd
```

GitHub 下载不通时，在能访问 GitHub 的机器下载三个文件，再用 `scp` 传到云电脑的普通用户
目录，校验并安装。不要关闭 TLS 校验。之后在 KDE 普通用户桌面终端执行：

```sh
systemctl --user restart spice-vdagent.service
dpkg-query -W spice-vdagent ydyun-usbctl
apt-cache policy spice-vdagent
apt-get -s install --only-upgrade spice-vdagent
```

预期安装版/候选版均为 `0.22.1-4.1+ydyun1`，官方包优先级为 `-1`，模拟升级不再替换它。
Discover 如仍显示之前的记录，点击刷新更新列表。重启 agent 会短暂影响分辨率协商或剪贴板，
无需重启整个云电脑。

## 后续发布

- 同一 Debian 基线的补丁迭代使用 `+ydyun2`、`+ydyun3` 等递增版本。
- 升级 Debian 基线时，如 `0.22.1-4.2+ydyun1`，先审阅上游差异和补丁兼容性，更新构建脚本
  固定版本，重新构建并验证 KDE Wayland、APT 候选选择和旧版迁移。
- 新 Release 附带两个 `.deb` 和校验文件；用户安装新补丁 `.deb` 即可升级，通常不用改 pin。
- 不覆盖已发布的资产，修复时发布新 tag，保留回退路径。

## 恢复 Debian 官方版本

恢复后会失去本项目 KScreen 补丁。先把规则备份到 APT 配置目录之外，再明确选择官方源版本：

```sh
mkdir -p ~/ydyun-apt-backup
sudo mv /etc/apt/preferences.d/ydyun-spice-vdagent.pref ~/ydyun-apt-backup/
sudo apt-mark unhold spice-vdagent
sudo apt update
apt-cache policy spice-vdagent
```

从上面输出选择官方源版本号，单独执行（这里 `0.22.1-4.1` 是本次验证的官方版本）：

```sh
sudo apt install --allow-downgrades spice-vdagent=0.22.1-4.1
sudo systemctl restart spice-vdagentd
systemctl --user restart spice-vdagent.service
```

如果以后重新安装补丁包而 pin 文件仍被记录为管理员删除，先把备份规则恢复到原路径，再安装。
