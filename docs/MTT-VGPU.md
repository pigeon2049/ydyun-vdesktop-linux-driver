# MTT S3000 vGPU：Debian 13 安装前调查

调查日期：2026-09-22。最新状态：**已在目标机编译并进行禁用硬件初始化的加载测试，
该社区构建拒绝 Guest 模式，GPU 加速未启用**。详见第 7 节。
补充：已下载并审计官方 MUSA SDK 4.0.1 / 4.3.0.CC2.1 随附的完整驱动 DEB，
两者内核构建都只接受 Native 模式；KUAE 云原生套件也未提供 Guest DEB。
这些包均未安装，详见第 8～9 节。
第 1～6 节记录安装前调查；没有替换目标机内核、显示配置或用户态图形库。

## 1. 目标机实测

| 项目 | 结果 |
| --- | --- |
| 系统 | Debian 13 trixie，x86_64 |
| 内核 | `6.12.107+deb13-amd64`，对应 headers 已安装，DKMS 未安装 |
| 桌面 | Plasma/KWin 6.3.6，Wayland，1920×1080 |
| 当前显示设备 | `00:02.0` Red Hat QXL，使用 `qxl` |
| 摩尔线程设备 | `00:0e.0` MTT S3000，PCI `1ed5:0222`，Subsystem `1ed5:1101` |
| GPU 驱动 | 摩尔线程设备无 driver 绑定；没有 `mtgpu` 模块或 `mthreads-gmi` |
| DRM 节点 | 只有 QXL 的 `/dev/dri/card0`，没有 `renderD*` |
| KWin 实际渲染器 | `llvmpipe (LLVM 19.1.7, 256 bits)`，Mesa 25.0.7，即 CPU 软件渲染 |

`lspci` 的 Region 2 `[size=16G]` 是 PCI BAR 地址窗口，不是可用显存。
Subsystem `1101` 与厂商的 `mtgpu-1101` 类型命名相符，**推测为 1 GiB 档位**；
不能仅凭 subsystem ID 确定实际配置。官方 Guest 示例中该类型报告 1024 MiB，
最终应以兼容驱动的 `mthreads-gmi` 或云平台配置为准。

## 2. 应装什么驱动

用户提供的 [S3000 服务器安装指南](https://docs.mthreads.com/driver-linux-server/driver-linux-server-doc-online/MTT_S3000/install_guide/)
介绍服务器驱动和 Xorg 配置，不能直接当作本机 vGPU Guest 的安装教程。

应优先取得 **MT_vGPU_LINUX_GUEST**，不是 Host 切分驱动，也不是直接套用
`musa_5.1.0-server_amd64.deb`。厂商 [vGPU 用户指南](https://docs.mthreads.com/vgpu/version-2.9.2/vgpu-doc-online/install_guide/)
列有 Linux Guest 的 DEB 安装方式，驱动获取渠道为 `developers@mthreads.com`。
本次未取得可验证的 Guest 安装包下载地址，未下载/执行来源不明的驱动。

必须先向中国移动云电脑或摩尔线程确认：

1. 当前宿主机 MT vGPU 驱动版本及允许使用的 Linux Guest 驱动版本。
2. Guest 包是否支持 Debian 13、Linux 6.12、x86_64，以及 Plasma 6 Wayland。
3. 此 vGPU 档位的实际显存、Linux 分辨率限制、图形/编解码能力。
4. 官方云桌面 Linux 端是否具有支持该 vGPU 的抓屏编码组件。

这是实质性兼容约束：[v2.9.2 发布说明](https://docs.mthreads.com/vgpu/version-2.9.2/vgpu-doc-online/releasenote/)
明确不兼容 2.5.x/2.6.x/2.7.x，兼容 2.9.0/2.9.1。
其 Linux Guest 已验证清单主要为指定 UOS/麒麟/中科方德和 CPU 组合，**未列 Debian 13**。
这不等于 Debian 必定不能用，但不能称为官方支持，也不能盲装最新版本。
Host 的内核兼容列表不能冒充 Guest 的兼容列表。

## 3. 取得匹配包后的安装流程（待实测）

以下是验证流程，不是目前已完成的操作，也不是可无条件复制执行的一键脚本。

1. 保留正常的 QXL、SDDM、SPICE 和 Tailscale，先验证普通用户 SSH + sudo。
   备份 `/etc` 中涉及显示/模块/启动的配置和软件包清单；如平台支持，先建立系统盘快照。
2. 在工作目录解包审计 Guest DEB：检查 `dpkg-deb -I`、`dpkg-deb -c`，用
   `dpkg-deb -e` 提取维护脚本；确认不会静默删除 QXL、替换显示管理器或覆盖 Mesa。
   核对厂商提供的校验值/签名以及驱动再分发许可。
3. 依据包要求准备构建依赖。若为 DKMS 包，通常需要 `dkms`、`build-essential` 和
   `linux-headers-$(uname -r)`。在匹配 Debian 13 内核的隔离构建环境先验证模块能否编译；
   编译通过不代表 Host/Guest 协议或 Wayland 已兼容。
4. 对选定的真实包路径先执行 `apt-get -s install ./实际文件名.deb`，审阅依赖变化。
   确认兼容且有回退路径后，才运行 `sudo apt install ./实际文件名.deb`。
   不使用 `--force-depends`，不往 Debian 混入 Ubuntu 软件源，不为试驱动直接降级现有内核。
5. 保存 DKMS/安装日志；安排可恢复的重启窗口。先确认 QXL 和官方客户端画面仍可用，
   再测试 GPU 渲染，最后才考虑桌面主 GPU 或编码链路。

安装后的分层验收：

```bash
# 模块和设备层
lspci -nnk -s 00:0e.0
lsmod | grep mtgpu
dkms status
ls -l /dev/dri
mthreads-gmi

# 在已登录的 KDE 普通用户会话中检查合成器，而非仅检查 glxinfo
qdbus6 org.kde.KWin /KWin supportInformation
```

确认 DRM/render 节点对应的是摩尔线程设备；不能把出现任意节点当作成功。
若只有模块加载而 KWin 仍显示 llvmpipe，桌面尚未获得硬件渲染。
再分别验证应用 OpenGL/EGL、Vulkan、视频解码和编码；Xwayland 应用加速不等于 KWin 加速。
涉及双 GPU 时还要验证缓冲区共享/拷贝路径，不能假设设置 `DRI_PRIME=1` 就一定有效。

若黑屏，通过保留的 SSH 恢复安装前配置，按审计记录卸载实际新增包并恢复被改动库文件；
必要时阻止新模块加载、更新 initramfs 后重启回到 QXL。具体回退命令必须根据实际包内容确定，
不预先给出可能误删系统图形库的通用 purge 脚本。

## 4. 对远程体验的作用

| 环节 | 当前情况 | vGPU 可能带来的作用/限制 |
| --- | --- | --- |
| 桌面、网页、3D 渲染 | KWin 使用 llvmpipe | 若 EGL/Wayland 和跨 GPU 输出兼容，可降低 CPU 开销、改善动画；收益待测 |
| 云端视频播放解码 | 未验证硬件解码 | 需要驱动和播放器/浏览器接入，不能只装模块 |
| 桌面抓屏与编码 | 当前 QXL/SPICE 画面正常，服务端编码实现未完全确认 | 需云桌面服务端支持摩尔线程编码；安装驱动不会自动把现有编码链路迁移到它 |
| 网络与本地解码 | 独立于 Guest 渲染 | GPU 不会直接降低网络 RTT，也不能消除客户端解码瓶颈 |

本项目的 `spice-vdagent` 补丁负责 KDE Wayland 分辨率协调，不是视频编码器。
保留官方客户端的前提下，优先探索 **GPU 渲染、QXL 保留输出**；这只是候选方案，
还没有验证 MT Guest 驱动与 KWin/QXL 的多 GPU 互操作。

不能贸然改成只在 MT 虚拟显示器输出：官方 [vGPU FAQ](https://docs.mthreads.com/vgpu/version-2.9.2/vgpu-doc-online/faq/)
说明 QEMU VNC 无法抓取 vGPU 桌面。它不等于本项目 SPICE 必定不能用，但提醒我们：
GPU 输出与现有远程采集路径不是天然互通，必须验证官方客户端实际看到的输出。

Wayland 并非原则上不支持硬件抓屏：厂商 [MTCapture 2.0](https://docs.mthreads.com/en/codec/codec-doc-online/capture_user_guide/)
说明支持 Xorg/Wayland，但其要求的 MUSA Driver ≥5.1.0 与 vGPU Guest 2.9.x 是不同版本线，
不能直接推断此云平台可用。更不能把单独运行的编码 demo 或另一个远程协议当作官方客户端适配完成。

另外，v2.9.2 用户指南中 Linux `1101` 档位列出的桌面/编解码上限为 1920×1200；
Windows 表中的 4K 能力不能照搬。实际仍以当前 Host/Guest 版本和配置为准。

## 5. 下一步与完成标准

当前卡点是**匹配的 Linux Guest 包和宿主版本信息**，不是缺少一条安装命令。
拿到后继续包审计 → 6.12 编译测试 → 模块加载 → 应用渲染 → Wayland/QXL → 官方客户端验证。
在同分辨率、同网络、同客户端设置下比较滚动/拖动/视频播放的 CPU、帧率、卡顿和输入延迟；
保留 GPU 使用率和编码会话证据，不预报百分比收益。
在这些步骤通过前，不把 GPU 驱动并入通用安装教程或 v0.2.52 Release。

## 6. GitHub 补充调查（2026-09-22）

检索 GitHub 仓库、代码、Issues、Release 和 fork 后，找到可研究的内核适配代码，
但本次没有找到经验证适用于当前云平台的完整 Linux vGPU Guest DEB。
搜索结果受索引范围限制，不能据此断言 GitHub 上绝不存在其他包。

| 仓库 | 已核对内容 | 对本项目的价值与限制 |
| --- | --- | --- |
| [dixyes/mtgpu-drv](https://github.com/dixyes/mtgpu-drv/tree/2.7.1-6.12) | `2.7.1-6.12` 分支的 `Readme.md` 明确称约适配 6.12；另有 2.7.0-6.12、2.7.1-6.13、3.0.0-6.14 分支 | 最值得参考的内核 API 适配；2026-01-16 已归档，不能当作当前维护的完整 Guest 发行版 |
| [elysia-best/mtgpu](https://github.com/elysia-best/mtgpu) | DKMS 版本 3.0.0，含 S3000 PCI ID、Guest 模式、虚拟显示调用 | 可研究驱动结构；未发现 Debian 13/本平台实测说明，无 Release |
| [cl91/mtgpu-drv](https://github.com/cl91/mtgpu-drv) | DKMS 版本 1.0.0，`v5.19-fix` 分支及 5.19 补丁，含 S3000/Guest 定义 | 较旧的内核适配参考，无 Release |

本地只读检查重点：

- dixyes 的 `2.7.1-6.12` 提交为 `099f7ea5afced34f9424618a9e2ec7aa37dba024`；
  `inc/mtgpu/mtgpu_defs.h` 定义 S3000 `0x0222` 和 `MTGPU_DRIVER_MODE_GUEST`。
- 该分支和 elysia 仓库的 Makefile 都链接 `objs/x86_64/mtgpu_core.o_binary`。
  因此不能把它们描述为完整可修改的开源驱动；闭源部分仍可能限制内核与 Host 协议适配。
- elysia 提交为 `118f2a970d8a7e9f6aadc122179c9d30323e94bc`，标记导入 3.0.0，
  构建配置却记录 2025 年的 Desktop 基线；仓库上传时间不是驱动核心发布时间。
- 上述代码树没有提供完整 EGL/OpenGL/Vulkan 用户态库及可直接安装的 Guest DEB。
  有 Guest 代码路径不等于当前二进制核心、固件和云平台协议已经匹配；
  DKMS 的 2.7.1/3.0.0 版本也不能直接等同于 MT vGPU 产品版本。
- [InfiniTensor/mudrv](https://github.com/InfiniTensor/mudrv) 是 Rust binding，
  不是替代内核驱动的安装包。
- [TheCreateGM/mtts30f43](https://github.com/TheCreateGM/mtts30f43) 自述面向 Fedora/S30，
  其脚本仍要求用户提供 MUSA DEB，不能充当本项目的现成 S3000 Guest 驱动。

三个候选仓库已克隆到工作区 `driver/doc/gpu-research/`（不使用 `/tmp`），
只进行静态检查，没有运行第三方安装脚本、编译或向目标机加载模块。
新增可行线索是：**Linux 6.12 的内核 API 适配已有社区参考**。
仍待解决的是配套用户态库/固件、Host 兼容性以及 Wayland/QXL 实测。

## 7. 实机编译与 Guest 模式测试（2026-09-22）

用户要求实机尝试后，使用第 6 节的 `099f7ea` 源码在目标机执行测试。
工作目录为 `/home/zgl/ydyun-gpu-test/`，普通用户构建，未使用 DKMS 安装脚本。
源码归档 SHA-256：`44b27ed296d8d8a825598acb78667691bc60d7cbffc8fcc56249be74b6206ef4`。

### 编译结果

原始分支在 Debian `6.12.107+deb13-amd64` 上失败：

```text
src/common/os-interface.c:2617: error: too few arguments to function 'pci_resize_resource'
```

目标机内核头文件的函数已是四参数版本。仅针对该目标版本，将封装改为：

```c
return pci_resize_resource(dev, resno, size, 0);
```

第四参数是无需释放的 BAR 掩码，0 表示没有额外排除项，参见
[内核 PCI API](https://www.kernel.org/doc/html/next/driver-api/pci/pci.html)。
这是目标版本实验补丁，不应未经特性检测直接用于其他内核。
构建命令为 `make -j4 ARCH=x86_64 KERNELVER=6.12.107+deb13-amd64`，
设置 `TMPDIR` 指向工作目录下的 `scratch/`，避免在 `/tmp` 存放解包内容。

修改后生成 `mtgpu.ko`，vermagic 与目标内核一致，SHA-256：
`f3b17e355e270e49c8af098c2c13d9948051467b7e63dbaecefe77fb208f1a73`。
构建仍出现 5138 条 objtool 警告，包括预编译核心的 `!ENDBR`，不能视为安全通过或生产可用。
没有关闭 IBT、模块签名检查或其他内核保护来绕过这些问题。

### 加载与模式验证

加载前静态检查发现 `inc/config_kernel.h` 中 `RGX_NUM_OS_SUPPORTED=1`，
`driver_mode_set()` 在此配置下只接受 Native 模式。因此未让它初始化 GPU 硬件。
通过设置 `disable_driver=1`，初始化函数在注册 PCI 驱动之前直接返回。

实机进行了两次临时加载，每次后卸载：

1. `disable_driver=1`：成功加载，日志确认 `build for Desktop` 和 `driver was disabled`。
2. `disable_driver=1 mtgpu_driver_mode=1`：加载失败，内核明确报告：

```text
mtgpu: mtgpu_driver_mode(1), setting error in this mode(native = -1)
mtgpu: `1' invalid for parameter `mtgpu_driver_mode'
```

**结论：该构建能够在目标内核形成并装入模块，但不能作为 Guest 驱动启用。
这不是缺少固件后的猜测，而是实机参数校验失败。**
目前尚未进行实际 GPU 绑定、渲染或编码测试，不能声称硬件加速成功。

不能仅修改宏或删除参数检查：该宏还影响多处结构布局，预编译核心也需要对应构建。
elysia 3.0.0 的构建配置同样是 `RGX_NUM_OS_SUPPORTED=1`，并含相同的模式检查，
因此未重复向目标机加载另一个 Native 构建。

### 收尾状态

- `mtgpu` 以及本次新加载的 DRM 辅助模块已卸载，S3000 无驱动绑定。
- 没有将实验模块写入 `/lib/modules`，没有配置开机加载，没有运行 `depmod` 或更新 initramfs。
- 未修改 QXL、Mesa、GRUB、SDDM，未重启机器；检查 SSH、Tailscale、SDDM、SPICE、
  KWin Wayland 和 PipeWire 服务均 active，输出仍为 1920×1080，渲染器仍为 llvmpipe。
- 加载未签名的树外模块使本次启动的 kernel taint 为 12288（O/E）；即使卸载标志仍保留，
  正常重启可清除。本次没有为清除该诊断标志打断桌面。
- 构建日志、加载日志及实验模块已取回本地 `driver/doc/gpu-research/target-results/`。
  此模块**不是可供用户安装的 Release 成品**。

下一步必须获得匹配云平台的 Guest 核心、固件及用户态驱动，再复用社区内核接口适配。

## 8. 官方 MUSA SDK 4.0.1 下载包审计（2026-09-22）

### 来源与保存位置

用户提供了官方开发者下载对象存储的限时签名链接，使用 GET 成功下载。
限时签名及凭据参数不写入公共文档；可从
[官方 SDK 4.0.1 下载页](https://developer.mthreads.com/sdk/download/musa?driverVersion=&equipment=&os=&version=4.0.1)
重新获取。下载源主机为 `mthreads-developer-download-prod.tos-cn-beijing.volces.com`。

本地文件均保留在工作区，没有使用 `/tmp`：

```text
driver/doc/gpu-research/
├── downloads/musasdk_v4.0.1_Intel_Ubuntu.zip
└── sdk-4.0.1/
    ├── MT_Linux_Driver_3.0.0/musa_3.0.0_amd64.deb
    ├── driver-control/   # 维护脚本，只读审计，未执行
    └── driver-root/      # dpkg-deb -x 提取的文件，未安装到系统
```

| 对象 | 大小 / SHA-256 |
| --- | --- |
| SDK ZIP | 1,822,531,763 字节；`6c91eab385d5d439b60d82f03b0fa30a94d4fa53763399d8eec95674d4172547` |
| 驱动 DEB | 164,682,270 字节；`79c30151354d0005ae5d760629aa2feabb470ec4ff5f4423df8ddc6544d02df6` |
| DEB 内预编译核心 `objs/x86_64/mtgpu_core.o_binary` | `1d4b559d4dfdd95baf06ce738c07f1f4d4237702f8f218bd4ade293845d07442` |

以上 SHA-256 为本地计算的复现标识，不冒充厂商公布的签名或校验值。
ZIP 的 `unzip -tq` CRC 检查及 DEB 内文件的 `md5sum -c` 检查均通过；
这些完整性检查不等于独立验证发布者签名。
ZIP 还包含 Toolkits 4.0.1、muDNN 2.8.1、MTML 2.0.0 和 PDF 文档；
本次仅解出驱动 DEB 及其内容，没有运行 Toolkits 或其他组件的安装程序。

### 实际包内容与 Guest 判定

`dpkg-deb -I` 显示包名 `musa`，架构 `amd64`，实际 Debian 版本字段为
`2025.03.26-33122-Ubuntu+9f9ebc2c1`，不能把文件名 `3.0.0` 当作 Debian 包版本。
依赖 `libdrm2 (>= 2.4.97)`、`dkms`、`libgles2`。

这个 DEB 确实比 GitHub 内核代码树完整：含 DKMS 源码/预编译核心、固件、
EGL/GLX/GLES/GBM/Vulkan 配置和用户态库、VA-API 库、`mthreads-gmi`，
以及 `MtCaptureDemo`、`MtCaptureToEncodeDemo`、`AppEncGL`。
但完整不等于适用于 vGPU Guest。具体证据相对于 `driver-root/usr/src/mtgpu-3.0.0/`：

- `inc/config_kernel.h:10`：`MT_BUILD_OS_TYPE "Desktop"`。
- `inc/config_kernel.h:27`：`RGX_NUM_OS_SUPPORTED 1`。
- `src/mtgpu/mtgpu_module_param.c:171` 起：只有宏大于 1 的分支接受 Host/Guest；
  此包走 `#else`，非 `MTGPU_DRIVER_MODE_NATIVE` 参数返回 `-EINVAL`。
- `Makefile` 仍需链接预编译的 `mtgpu_core.o_binary`，不是可任意切换模式的完整源码。

**结论：本次取得的官方 Intel/Ubuntu 3.0.0 包仍是 Native 构建，不是我们所需的
`MT_vGPU_LINUX_GUEST`。不应通过删除参数检查或单独改宏，把它强装成 Guest。**
本节结论来自实际下载包的静态检查，不冒称已经在目标机测试过这个 3.0.0 包。
其核心 SHA-256 与此前 elysia 仓库的核心不同，也不应声称二者二进制完全相同。

[官方 SDK 4.0.1 发布说明](https://docs.mthreads.com/musa-sdk/version-4.3.x/releasenote/MUSA%20SDK%204.0.1%20releasenote/)
说明随 SDK 打包提供 `musa_3.0.0_amd64.deb`，并列出 Intel + Ubuntu 22.04 的兼容环境。
SDK 4.0.1、通用驱动 3.0.0 与 MT vGPU 2.9.x 是不同版本字段，不能互相替代。

### 为什么没有直接 dpkg 安装

维护脚本不只是复制文件：会进行 DKMS 构建/安装，配置 ALSA，应用 sysctl，
更新动态库缓存、模块依赖和所有内核的 initramfs；DKMS post-install hook 还会临时
以 `disable_driver=1` 加载模块。包也带有全局动态库搜索路径、EGL/Vulkan 注册等配置。
在 Guest 模式已经不匹配的情况下，执行这些操作没有必要且会扩大回退范围。

本次未向目标机上传或安装该包，未修改本机/目标机系统库或模块，未重启。
抓屏/编码示例暂只确认存在，没有运行，也未验证它们是否支持当前 Wayland、Guest 或官方客户端链路。

### 其他用户提供的入口

- [知乎文章](https://zhuanlan.zhihu.com/p/691273187) 已通过真实 Chrome 读取。
  文章的驱动入口指向官方 `/pes/drivers/search`，另外引用 `MooreThreads/torch_musa`
  和 SDK 下载页。文章是通用 GPU/PyTorch/容器安装线索，未给出本机所需 Guest 包。
- [PES 页面](https://www.mthreads.com/pes/drivers/index) 的“立即下载”同样指向产品驱动搜索。
  PES 控制中心、通用驱动下载入口与 vGPU Guest 包应区分；仅访问入口不能证明找到了 Guest 包。
- [MooreThreads GitHub 组织](https://github.com/MooreThreads) 已打开查看；
  本轮未完成其所有仓库和附件的逐项审计，不据此断言官方 GitHub 完全不存在驱动资源。

仍需匹配云平台 Host 的 Linux Guest 驱动，不能把这个 Native DEB 并入项目 Release。

## 9. SDK 4.3.0.CC2.1 与 KUAE 云原生套件 2.1.0（2026-09-22）

用户继续提供两个官方限时链接，均完成下载并通过 ZIP CRC 检查。
源文件放在 `driver/doc/gpu-research/downloads/`，SDK 驱动提取到 `sdk-4.3.0/`，
KUAE 解包到 `kuae-2.1.0/`。没有执行安装程序或部署 Kubernetes 资源。

| 文件 | 大小（字节） | 本地 SHA-256 |
| --- | ---: | --- |
| `MUSA_SDK_4.3.0.CC2.1.zip` | 2,478,210,217 | `c4cb8c7722a5ed2c30b69bf09cec88abf3a3a531dd599f077b0312b5d847d7fd` |
| `musa_3.3.0-server_amd64.deb` | 161,650,308 | `70693c934019e83b45eff478411a1b706118618851f163f7e28959984fc2b059` |
| `KUAE_Cloud_Native_Toolkits+v2.1.0.zip` | 33,158,662 | `b1c7424ddce74043651540e267cb84414ea90a0d41caeb9c0f2dc59b9c5b1637` |

### SDK 4.3.0：server 文件名不代表 Guest 构建

ZIP 中驱动为 `MT_Linux_Driver_3.3.0/musa_3.3.0-server_amd64.deb`，
包名 `musa`、版本字段 `3.3.0-server`、架构 `amd64`，依赖 `libdrm2` 和 DKMS。
提取后，DEB 内置 MD5 清单检查通过。

需要注意它的 DKMS 源码目录仍叫 `usr/src/mtgpu-3.0.0/`，不能依据目录名推断包版本。
在该目录中再次找到直接证据：

- `inc/config_kernel.h:10` 仍为 `MT_BUILD_OS_TYPE "Desktop"`。
- `inc/config_kernel.h:27` 仍为 `RGX_NUM_OS_SUPPORTED 1`。
- `src/mtgpu/mtgpu_module_param.c:198` 起的模式分支，当前配置只接受 Native（-1），
  Guest（1）会返回 `-EINVAL`。
- Makefile 强制包含该配置头文件，并链接预编译 `objs/x86_64/mtgpu.core.o_binary`；
  核心 SHA-256 为 `90875011f73a2702b86628ce428871c40fe376893a6a87b2d2a15f5d5a119deb`。

因此**这个实际下载的 3.3.0-server 包也不是所需 Guest 构建**。
并非仅因它标记 Ubuntu/server 而排除，也不是说所有版本的服务器驱动都必然如此。
DEB 内有 EGL/GLX、固件和抓屏编码示例，但不弥补 Guest 核心缺口。
维护脚本仍包含 DKMS、模块加载、sysctl、动态库及 initramfs 更新，未执行。

[官方 4.3.0 版本说明](https://docs.mthreads.com/en/musa-sdk/musa-sdk-doc-online/history_version/rc4.3/releasenote/MUSA%20SDK%204.3.0%20releasenote/)
的组件表列出 3.3.0-server，而其依赖段仍出现旧 2.7.0 文件名；
因此本次以下载 ZIP、DEB 元数据和实际配置为依据，不拿页面中的版本混用来指导安装。

### KUAE 2.1.0：容器/集群层，不替代 Guest 驱动

实际 ZIP 主要内容：

- `mt-container-toolkit_2.1.0-1_amd64.deb` 及 RPM。
- MTML 2.1.0 的 DEB/RPM。
- `sgpu-dkms_1.3.0_amd64.deb` 及 RPM。
- GPU Operator 的 Helm/YAML、镜像同步脚本和两份使用文档。

`Container-Toolkit/end-user-cn.md:48` 明确要求预先安装显卡驱动并验证 GMI。
其 Debian 12 支持条目是容器套件支持清单，不能据此声称 vGPU Guest 支持 Debian 13。

进一步提取并检查 sGPU 源码：`sgpu.c` 打开 `/dev/mtgpu.*` 等已有 GPU 节点，
查询主驱动的符号，并在第 1075 行声明 `MODULE_SOFTDEP("pre: mtgpu")`。
所以 `sgpu-dkms` 不是独立驱动云端 PCI vGPU 的替代方案。
提取出的源码目录缺失所有者执行/遍历位，审计时仅对本地提取目录补了 `u+x`；
未修改原始 DEB、系统权限或目标机。

GPU Operator 文档确实包含 vGPU、KubeVirt、直通和 Host 节点驱动部署功能，
不能说整个套件完全与虚拟化无关。但它通过配置的 OSS/HTTP/本地来源另取驱动，
**本 ZIP 中没有 `MT_vGPU_LINUX_GUEST` 安装包**；管理宿主 GPU 资源不等于补齐虚拟机内的驱动。
文档中的 OSS 示例使用凭据占位符，不是已获取的公开 Guest 下载地址，未尝试绕过鉴权。

### 本轮结果

三个用户提供的下载包均已保存并检查。获得了两个完整 Native 驱动包及容器工具，
但尚未获得匹配的 Guest 驱动。没有改动目标机、重启或发布这些厂商二进制。
下一步依然是拿到匹配云平台 Host 的 Linux Guest 包，再验证 Debian 13/6.12、Wayland 和官方客户端。

## 10. “原包不支持 Guest”不等于“绝无改造可能”

用户追问现有包能否改造后，进一步只读检查 3.3.0 的预编译核心符号。
`nm -S --defined-only` 确认其中保留了 `GuestDevicePAddrToHostDevicePAddr`、
`VMMCreatePvzConnection`、`PvzConnectionInit`、`mtgpu_guest_get_host_vpu_support_version`
等定义。因此不能声称这个二进制完全不含 Guest 相关逻辑；符号存在也不能证明完整可用。

同时，`inc/mtgpu/mtgpu_vgpu_ipc.h` 只有在 `RGX_NUM_OS_SUPPORTED > 1` 且
`PVRSRV_APPHINT_DRIVERMODE == 1` 时声明真正 Guest IPC 接口；否则提供返回 0/false
或不执行操作的内联桩。该核心的已定义符号列表未找到 `mtgpu_vgpu_ipc_*`，
当前可见源码中也未找到这些接口的非桩实现。这里只说明这些具体接口的缺口，
不据此断言不存在另一条通信实现路径。

对第 8～9 节结论的精确定义是：**原始配置只接受 Native，尚无证据证明简单改参数/宏后
可得到功能完整且与目标 Host 兼容的 Guest 驱动；并未证明逆向改造在技术上绝对不可能。**

可继续研究的方向包括：离线核对 Guest 函数调用链与桩函数、可见头文件和二进制结构布局、
地址转换/中断/共享内存协议、用户态接口及 Host 版本。若需要修改/构建/加载实验版，
应作为独立验证阶段，不能把“参数检查通过”或“模块能装入”当作 GPU 可用。
此次解释阶段仅执行静态检查，没有改驱动代码或加载模块。
