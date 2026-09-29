# 请求云平台提供 Linux vGPU Guest 适配材料

本云电脑已安装 Debian 13.7 amd64，Linux 6.12.107+deb13-amd64，使用 KDE Plasma Wayland。
Guest PCI 设备为 Moore Threads S3000，PCI ID `1ed5:0222`，subsystem `1ed5:1101`。

请提供：

1. 此实例所在 Host 的 MT vGPU 驱动完整版本，以及兼容的 Linux Guest 版本。
2. 相匹配的 `MT_vGPU_LINUX_GUEST` amd64 DEB 或完整构建包、发布校验值和依赖说明。
3. 该包对 Debian 13 / Linux 6.12 的支持情况；如不支持，提供 Guest 内核适配源码及匹配的预编译核心。
4. 与该核心配套的 EGL/OpenGL/Vulkan、VA-API/编码库及需要的固件。
5. 此 `1101` 档位的实际显存、图形/编解码能力和限制，及是否支持 GPU 渲染、QXL 输出的组合。

现有 Windows 驱动 `mtkm64.sys` 文件版本是 `30.0.2505.1`，产品版本 `1.14.593.6196`；
PDB 路径包含 `M-vdi-guest275`，请核对它是否对应平台 2.7.5 分支。
这仅是包内线索，尚未确认 Host 版本。现有 Linux Native 驱动核心不支持虚拟化初始化，
不能替代真实 Linux Guest 驱动。

此文本仅保存在本机，尚未发送给云平台或摩尔线程。
