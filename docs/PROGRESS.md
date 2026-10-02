# 云电脑 USB / 画面线当前状态（2026-10-03）

本文件只讲现状。完整 Step 0–140 日志（含历次构建 SHA 与实验细节）在
[`PROGRESS-HISTORY.md`](PROGRESS-HISTORY.md)（只读归档）。
vGPU 线的状态不在这里，在仓库根 [`STATUS.md`](../STATUS.md)。

## 可交付物：v0.2.52

- `ydyun-usbctl_0.2.52-1_amd64.deb`：USB/IP 控制器（标准 `DEVLIST/IMPORT`、
  VHCI、`usb-storage`/`usbhid` 接管；ZTE 兼容桥默认关闭）。
- `spice-vdagent_0.22.1-4.1+ydyun1_amd64.deb`：KDE Plasma Wayland KScreen 适配
  版（独立版本号 + APT 保护规则，防官方包覆盖）。
- 安装教程见仓库根 [`README.md`](../README.md)（DD → 通用内核 → KDE →
  Release 包 → 验证）；升级策略见 [`UPDATES.md`](UPDATES.md)。

## 已验证（本机，离线/回环）

- vUDC 复合 gadget（键盘+鼠标+存储）经 `usbipd → vhci-hcd` 闭环：
  HID 报告到达 evdev，存储扇区写入/回读一致；测试状态已清理。
- 标准 SPICE display stream（H.264 样本）→ `ydyun-ice-probe` 提取 →
  FFmpeg 解码闭环；不涉及厂商私有建链。
- 包审计：Depends 仅 `python3, usbip`；无 `.exe/.dll/.sys/.pdb`、
  无安全/监控/遥测/网络拦截组件；三个 systemd 单元默认
  `disabled/inactive`。

## 未完成（需真实云会话，不猜测）

- 合法会话中的 `probe/list`、storage/HID 的 attach/热插拔/重连/卸载实测。
- ZTE 控制面（3246/5100/19000）真实控制帧与压缩样本确认。
- 真实 ICE/SPICE display 样本的 codec/surface/stream 确认与解码原型。
- 约束见 [`SECURITY-SCOPE.md`](SECURITY-SCOPE.md)：
  不携带、不执行、不猜测闭源会话/安全/QoE 组件。

## 证据文档（静态分析，按需读）

| 文件 | 内容 |
|---|---|
| `ANALYSIS.md` | Windows 安装包静态分析（Step 0–40） |
| `CLIENT-ANALYSIS.md` / `CLIENT-ABI.md` | 官方 Linux 客户端 DWARF/符号：display/input/USB 分层、JWAE/SCG/ZIME 边界 |
| `USB-FRONTEND-ANALYSIS.md` | 官方本地 USB 策略层（SysV 队列，非云端协议） |
| `ICE-PROTOCOL.md` | Windows ICE display channel 静态调查 |
| `WAYLAND-RESOLUTION.md` | KScreen 适配设计 |
