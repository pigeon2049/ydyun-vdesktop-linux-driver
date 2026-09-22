# Windows 全开源客体端审计记录

日期：2026-09-22

## 决策

Windows 客体端不再复用任何 ZTE/ICE 原厂二进制。原厂包仅作为协议和硬件调查证据，不作为
构建输入、安装输入或回退方案。

## 为什么 Proxmox VirtIO-Win 路线可行

`CloudBackup.zip` 的 `driver-list.txt` 证明原 Windows 已存在 Red Hat 提供者的：

- `vioser.inf`；
- `vioinput.inf`；
- `balloon.inf`、`vioscsi.inf`、`viostor.inf`、`netkvm.inf`。

Linux 实机已经能绑定并输出 QXL。因此当前虚拟机不是只能依赖 ICE 私有虚拟硬件，存在标准
QEMU/KVM 客体设备基础。Proxmox 的 Windows VirtIO 文档最终使用同一 VirtIO-Win 驱动来源，
可以用来准备安装 ISO。

这不等于“只要装 ISO 就一定工作”。宿主仍必须实际暴露 QXL/VirtIO GPU、VirtIO serial 和
HDA/AC97；安装器和 `ydyun-guestctl doctor` 都会检查这些设备。

## 固定源码

| 组件 | 固定提交 | 许可证 | 用途 |
|---|---|---|---|
| `virtio-win/kvm-guest-drivers-windows` | `233a5895364a90278a69710f3ce0f2a21b1caa4b` | BSD-3-Clause | vioser、vioinput、viogpudo |
| `vrozenfe/qxl-dod` | `dd8fc1a4520006f648ea9ec02bc646eda392cbee` | Apache-2.0 | QXL WDDM DOD |
| `nefarius/vd_agent` | `2d77558aed4acb841c23f049ea31c2603e0f882e` | GPL-2.0-or-later | 剪贴板、分辨率、鼠标集成 |
| `vadimgrn/usbip-win2` | `83bd1f781d57ed6efdf15530c55710cf5d4482bc` (`v.0.9.8.0`) | BSD-2-Clause | 可选 USB/IP client |

机器可读版本保存在 `windows/dependencies.lock.json`。参考源码保存在主仓库外的
`driver/doc/reference/windows-open-source/`，不参与项目提交和 Release。

## 发布包策略

仓库不提交 VirtIO-Win ISO、SPICE agent MSI 或可选 USB/IP 安装器，因此当前源码仓库本身不是
“下载后立即安装”的 payload 包。Windows 构建机挂载 ISO、准备经过签名或固定 SHA-256 的 MSI
后，执行 `windows/Build-OpenSourcePackage.ps1`，会生成唯一的
`dist/ydyun-windows-open-source.zip`。ZIP 内含安装脚本、诊断工具、许可证/锁定信息、payload、
`manifest.json` 和 `SHA256SUMS`；`windows/Install-YdyunOpenGuest.ps1` 是最终用户的一键入口。

打包器拒绝覆盖已有输出目录，不联网下载依赖，并在复制前拒绝路径或 INF 内容带有 ZTE/ICE
标记的文件。

## 账户 API 扫描

对以上四个固定源码树扫描以下符号和命令，结果均为零匹配：

- `NetUserAdd`、`NetUserSetInfo`、`NetUserDel`、`NetUserChangePassword`；
- `NetLocalGroupAdd*`、`NetLocalGroupDel*`、`NetLocalGroupSet*`；
- `LsaStorePrivateData`；
- PowerShell 本地用户、密码和组成员修改命令；
- `AutoAdminLogon`、`DefaultPassword`。

这与原厂 `Vdagent.exe` 已确认存在上述账户 API 的结果形成直接对照。

## 本项目构建验证

`ydyun-guestctl.exe` 在 Debian 13 使用 MinGW x86_64 编译，编译参数包含
`-Wall -Wextra -Wpedantic -Werror`。生成物为 PE32+ x86-64 console，导入 DLL 仅有：

- `ADVAPI32.dll`；
- `KERNEL32.dll`；
- `MSVCRT.dll`；
- `SETUPAPI.dll`。

未导入任何用户创建、密码设置或本地组修改 API。CMake 交叉构建也已通过。
两个 PowerShell 脚本已用 PowerShell 7.6.6 parser 完成语法解析，账户边界源码测试通过。

## 明确排除

- 所有 ICE/ZTE 显示、输入、声音、隧道和 USB 驱动/服务；
- 原厂 `Vdservice/Vdagent`、Credential Provider、AdAs、ZProcessMonitor；
- QEMU Guest Agent：本项目不向宿主开放客体命令执行面；
- VirtIO balloon service、WebDAV、共享目录和一键全装 Guest Tools；
- 测试签名模式和跳过 Windows 驱动签名检查。

## 剩余实机验证

1. 干净 Windows 中的实际 PCI/PNP 设备 ID；
2. QXL/VirtIO GPU 驱动加载后的官方客户端画面；
3. VirtIO serial 上是否存在 `com.redhat.spice.0`；
4. 开源 SPICE agent 的剪贴板和全屏/窗口分辨率行为；
5. 宿主是否暴露 HDA/AC97 并能传出声音；
6. USB 走宿主 SPICE 重定向还是标准 USB/IP endpoint。

任一关键宿主设备缺失时，结论是需要云平台宿主配置支持，不允许用原厂组件填补。
