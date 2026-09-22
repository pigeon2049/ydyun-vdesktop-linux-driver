# v0.2.52：修复补丁包被官方更新覆盖的问题

旧补丁沿用 Debian 官方版本 `0.22.1-4.1`，APT/Discover 会把元数据不同的官方构建列为更新。
本版使用独立版本，并随补丁包安装只针对 `spice-vdagent` 的 APT 保护规则。

- `spice-vdagent_0.22.1-4.1+ydyun1_amd64.deb`：KDE Wayland KScreen 补丁、`kscreen` 依赖、
  APT 更新策略；后续 `+ydyunN` 包可以正常升级。
- `ydyun-usbctl_0.2.52-1_amd64.deb`：更新安装、迁移和维护文档，USB 运行时代码未修改。
- `SHA256SUMS`：以上两个包的校验值。

适用 Debian 13 trixie amd64。从 v0.2.51 升级无需重装系统；若以前设置了 hold，先取消，
校验后安装两个包并重启 agent。详见[更新步骤与恢复官方版](https://github.com/pigeon2049/ydyun-vdesktop-linux-driver/blob/v0.2.52/docs/UPDATES.md)。

目前通过 GitHub Releases 手动分发，没有自动更新 APT 仓库。官方 `spice-vdagent` 更新会被
拦截，包括安全更新，需要在合并和验证补丁后发布新的下游包；其他系统更新不受影响。

验证：补丁包上游 3 项测试、控制器 54 项测试通过；真实 APT 隔离模拟通过旧版迁移、未来
官方包拦截、后续补丁升级、其他包正常更新和官方版恢复。目标 Debian 13 机器已完成升级，
APT 候选为 `0.22.1-4.1+ydyun1`，官方包优先级 -1，模拟整机升级无替换项；运行时日志确认
`KScreen current Virtual-1 1920x1080+0+0`，Wayland 和音频服务保持运行。
