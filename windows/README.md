# Windows 客体端：100% 开源实现

本目录不使用、不复制、不安装任何 ZTE/ICE/中国移动云电脑原厂驱动或服务。Windows 客体端改为：

| 功能 | 开源组件 | 客体内所需宿主设备 |
|---|---|---|
| 画面 | QXL WDDM DOD 或 VirtIO GPU DOD | QXL `1b36:0100` 或 VirtIO GPU `1af4:1050` |
| 鼠标键盘 | Windows inbox USB/PS2 或 `vioinput` | USB tablet/keyboard 或 VirtIO input |
| 剪贴板、自动分辨率 | `nefarius/vd_agent` | VirtIO serial + `com.redhat.spice.0` |
| 声音 | Windows inbox HDA/AC97 | 宿主暴露的虚拟声卡；VirtIO-Win 当前没有 Windows virtio-snd 驱动 |
| USB | SPICE usbredir，或可选 `usbip-win2` | 宿主 USB 重定向通道或授权的 USB/IP endpoint |

客体软件不能凭空创建宿主没有提供的 QXL、VirtIO serial、虚拟声卡或 USB 通道。安装器先检查
硬件 ID；缺少关键设备时直接停止，不回退原厂组件。

## Proxmox VirtIO 驱动能不能用？

能用，而且是本实现的首选二进制来源。Proxmox 文档引用的是 Fedora/Red Hat 构建的
VirtIO-Win ISO；驱动源码来自 `virtio-win/kvm-guest-drivers-windows`。从 ISO 中只取与当前硬件
匹配的以下目录：

- `vioserial\w10\amd64`：SPICE 通道必须；
- `vioinput\w10\amd64`：宿主暴露 VirtIO input 时安装；
- `qxldod\w10\amd64`：Windows 10 + QXL；
- `viogpudo\w10\amd64` 或 `viogpudo\w11\amd64`：宿主暴露 VirtIO GPU 时使用。

不要直接运行 ISO 根目录的“一键 Guest Tools”全装程序。本项目特意不安装：

- QEMU Guest Agent：宿主可以经它请求客体执行命令，不符合本项目的账户与远程控制边界；
- Balloon service、WebDAV、共享目录、遥测和日志代理；
- 网络和系统盘驱动：当前系统已经能启动和联网，不在远程桌面适配范围内，误换会增加失联风险。

Proxmox/VirtIO-Win 解决的是客体驱动，不会把当前云平台变成 Proxmox，也不要求改动宿主。能否
工作取决于当前云平台是否已经暴露标准 QEMU/KVM 设备。既有备份中能看到 Red Hat 的
`balloon/vioser/vioinput/vioscsi/viostor/netkvm` 驱动，Linux 实机也已使用 QXL，因此路线有明确
硬件依据；最终仍须在干净 Windows 快照里实测。

## 唯一允许的上游

- VirtIO Windows 驱动：<https://github.com/virtio-win/kvm-guest-drivers-windows>
- VirtIO-Win 打包脚本：<https://github.com/virtio-win/virtio-win-pkg-scripts>
- QXL WDDM DOD：<https://github.com/vrozenfe/qxl-dod>
- Windows SPICE agent：<https://github.com/nefarius/vd_agent>
- 可选 USB/IP client：<https://github.com/vadimgrn/usbip-win2>

发布时必须记录源码 tag/commit、二进制 SHA-256 和签名状态。仓库不重新分发未经允许的第三方
二进制，安装器也不会联网下载“latest”。

## 账户安全硬边界

安装器执行以下规则：

1. 只要发现任一 ZTE/ICE 服务或内核驱动，就拒绝混装；
2. 只允许 `qxldod.inf`、`viogpudo.inf`、`vioser.inf`、`vioinput.inf`；
3. INF 路径或内容出现 ZTE/ICE 标记即拒绝，防止使用原厂重签名版本；
4. SPICE/USB 安装包必须由调用者提供固定 SHA-256，默认还必须通过 Authenticode 验证；
5. 禁用 Remote Registry、WinRM 和自动登录，删除 Winlogon 中缓存的自动登录密码；
6. 明确拒绝 SPICE agent 写入 Winlogon；
7. 安装前后比较本地用户、密码最后修改时间和本地组成员，任何变化立即失败；
8. 源码和最终 PE 导入表都检查账户创建、改密码和组成员修改 API。

脚本本身不会新增、删除、启用、禁用或重命名用户，不会修改任何用户密码，也不会修改本地组。

## 准备文件

### 1. VirtIO-Win ISO

按 [Proxmox Windows VirtIO Drivers](https://pve.proxmox.com/wiki/Windows_VirtIO_Drivers) 的链接获取
稳定版 VirtIO-Win ISO，在 Windows 中挂载为光驱。也可从 VirtIO-Win 官方下载页取得稳定 ISO。

不要使用先前 `CloudBackup.zip` 中导出的 VirtIO 驱动：它们的签名者显示为 ZTE，违反新的
“零原厂组件”要求。

### 2. 开源 SPICE agent

从 `nefarius/vd_agent` 的已签名 Release 取得 x64 MSI，或从固定提交自行构建 MSI。计算哈希：

```powershell
$spiceMsi = 'C:\Install\vdagent-win-x64.msi'
$spiceHash = (Get-FileHash $spiceMsi -Algorithm SHA256).Hash
```

自行从锁定提交构建的未签名 MSI 必须显式加 `-AllowUnsignedSourceBuild`；正式发布和日常安装不允许
使用这个开关。

不能用 `ZTEGuestOS\vdservice\Vdagent.exe`。虽然文件名相似，它是另一套闭源程序，已确认含用户
创建、密码设置、管理员组修改和自动登录逻辑。

### 3. 可选 USB/IP

优先先测试官方客户端的标准 USB 重定向；如果宿主直接提供 USB 设备，Windows 不需要额外
USB/IP 驱动。只有已经确认云端提供标准 USB/IP endpoint 时，才安装 `vadimgrn/usbip-win2`。

`usbip-win2` 要求 Windows 10 1903（build 18362）或更高。原备份系统是 Windows 10 Enterprise
LTSC 2019 build 1809，因此不能在该旧系统上启用它；应换成更新的 Windows 10/11，或只用宿主
SPICE USB 重定向。不要开启测试签名模式，使用其 Microsoft 签名/WHLK 构建。

## 安装前硬件检查

在干净 Windows 的 64 位管理员 PowerShell 中：

```powershell
Get-CimInstance Win32_PnPEntity |
  Select-Object -ExpandProperty DeviceID |
  Select-String 'VEN_1B36&DEV_0100|VEN_1AF4&DEV_1050|VEN_1AF4&DEV_1003|VEN_1AF4&DEV_1043|HDAUDIO'
```

至少应看到一种显示设备、一种 VirtIO serial 设备和标准 HDA/AC97 声卡。如果没有，停止：这
不是换一个 INF 能解决的问题，必须由云平台宿主暴露对应设备。

## 安装

### 直接生成一个可交付安装包

在 Windows 构建机上挂载 VirtIO-Win ISO，并准备已签名的开源 SPICE agent MSI：

```powershell
.\windows\Build-OpenSourcePackage.ps1 `
  -VirtioRoot 'D:\' `
  -SpiceAgentMsi 'C:\Install\vdagent-win-x64.msi' `
  -OsFamily w10
```

生成 `dist\ydyun-windows-open-source.zip`。如果已经确认需要标准 USB/IP endpoint，可以增加：

```powershell
-UsbIpInstaller 'C:\Install\usbip-win2-x64.exe'
```

最终包内的 `resources\Install-YdyunOpenGuest.ps1` 会自动识别系统版本、计算 payload 哈希并调用
安全安装器；用户只需要在管理员 PowerShell 执行：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\windows\Install-YdyunOpenGuest.ps1
```

如果重装后只能使用键盘，解压后目录只有一个 BAT 和一个 `resources` 资源目录。在 CMD 中切换
到解压目录，直接执行下面的批处理文件即可。它会自动
请求管理员权限并停留显示安装结果：

```bat
Install-YdyunOpenGuest.bat
```

生成器不会覆盖已有输出目录，也不会从网络下载 latest 文件。

假设 VirtIO-Win ISO 是 `D:`，系统为 Windows 10：

```powershell
Set-ExecutionPolicy -Scope Process Bypass

$spiceMsi = 'C:\Install\vdagent-win-x64.msi'
$spiceHash = (Get-FileHash $spiceMsi -Algorithm SHA256).Hash

.\windows\Test-AccountBoundary.ps1
.\windows\Install-YdyunGuest.ps1 `
  -DriverRoot 'D:\' `
  -OsFamily w10 `
  -SpiceAgentMsi $spiceMsi `
  -SpiceAgentSha256 $spiceHash
```

安装器递归查找对应 `w10\amd64` 文件，只安装允许列表。Windows 11 改为 `-OsFamily w11`。

如果已经确认需要 USB/IP，且系统 build 不低于 18362：

```powershell
$usbInstaller = 'C:\Install\usbip-win2-x64.exe'
$usbHash = (Get-FileHash $usbInstaller -Algorithm SHA256).Hash

.\windows\Install-YdyunGuest.ps1 `
  -DriverRoot 'D:\' -OsFamily w10 `
  -SpiceAgentMsi $spiceMsi -SpiceAgentSha256 $spiceHash `
  -EnableUsbIp -UsbIpInstaller $usbInstaller -UsbIpSha256 $usbHash
```

## 构建诊断工具

Visual Studio 2022 x64 Native Tools：

```powershell
cmake -S windows -B build-windows -A x64
cmake --build build-windows --config Release
```

Linux 上可交叉编译复核：

```sh
sudo apt install g++-mingw-w64-x86-64
mkdir -p build-windows
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode \
  windows/src/ydyun_guestctl.cpp -ladvapi32 -lsetupapi -static \
  -o build-windows/ydyun-guestctl.exe
```

用途：

```powershell
.\ydyun-guestctl.exe doctor
.\ydyun-guestctl.exe install-drivers --root D:\
.\ydyun-guestctl.exe start
.\ydyun-guestctl.exe stop
```

`doctor` 必须同时满足：显示设备、VirtIO serial 和标准音频设备存在，`spice-agent` 正在运行，
所有 ZTE/ICE 服务均不存在。

## 重启后验收

1. 官方客户端能显示 Windows 登录页和桌面；
2. 使用原来创建的本地用户和原密码，账户列表与管理员组没有变化；
3. 鼠标键盘正常，全屏/还原触发分辨率变化，双向剪贴板正常；
4. 设备管理器中的 QXL/VirtIO 设备无黄色感叹号；
5. `spice-agent` 正常，系统中不存在 ICE、ZTE、QEMU Guest Agent 服务；
6. 检查是否有 Windows inbox HDA/AC97 声卡并实际播放声音；
7. USB 存储只在用户主动重定向/attach 后出现，断开后消失。

声音是当前唯一不能由 VirtIO-Win 客体包补齐的部分：上游目前没有 Windows virtio-snd 驱动。
若 Windows 看不到任何 HDA/AC97 设备，需要宿主增加标准虚拟声卡；不要用原厂虚拟声卡替代。

固定源码与扫描结果见 [Windows 全开源客体端审计](../docs/WINDOWS-OPEN-SOURCE-AUDIT.md)。
