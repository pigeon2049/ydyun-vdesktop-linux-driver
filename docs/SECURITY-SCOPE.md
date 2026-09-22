# 安全边界与排除项

本项目只实现操作者明确需要的 Linux 数据面，不把官方客户端中的所有组件整体搬运
到 Debian：

保留：

- 标准 SPICE display、inputs、cursor、playback 边界；
- Linux 内核 `usbip-core`、`vhci-hcd`、`usbip-host`、`usb-storage`、`uas`、`usbhid`；
- 显式 bus ID 的 attach、detach、export 和本地回归工具；
- KDE KScreen Wayland 分辨率适配。

排除：

- Windows `.sys` 驱动和 WinDivert；
- 官方安全库、SysGuard、设备黑名单策略服务；
- QoE、trace、网络质量、公网探测和进程监控；
- `usbredirect` root 策略前端、SysV 队列和自动接管实体设备；
- 未验证的 JWAE/SCG 私有认证、token 生成和云端绕过逻辑。

默认 systemd 单元全部 `disabled`，物理 USB 设备只有在用户显式配置并执行导出时才会
解除本地驱动。文档中的密码、token、auth code 只能以字段名或占位符出现；任何真实
会话材料都不得提交仓库。
