# KDE Plasma Wayland 适配

Debian trixie 的 `spice-vdagent 0.22.1` 原生支持 X11/XRandR；其 Wayland 路径主要
尝试 GNOME Mutter 的 `org.gnome.Mutter.DisplayConfig`。KDE Plasma 使用 KWin，因此
官方 agent 在 KDE Wayland 中无法查询和设置输出。

本目录提供一个针对上游 `spice-vdagent` 0.22.1 的最小补丁：

1. 检测 `XDG_SESSION_TYPE=wayland`。
2. 无 shell 执行 `kscreen-doctor -o`，读取当前连接的 KScreen 输出和可用模式。
3. 收到 SPICE `VDAgentMonitorsConfig` 时选择精确模式；如果 QXL 没有该模式，选择
   几何距离最近的已公布模式，避免向 KWin 请求不可驱动的模式。
4. 将当前几何回报给原有 `spice-vdagentd`，保持 agent 的输入、剪贴板和其他能力。

它目前针对云电脑常见的单个 QXL 输出；多输出映射需要继续把 SPICE display ID 与
KScreen connector 做完整映射。补丁不会启动额外 root 守护进程，不会读取认证信息，
也不会修改安全、监控和 QoE 组件。
