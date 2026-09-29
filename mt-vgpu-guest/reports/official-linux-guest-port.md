# 摩尔线程 Linux vGPU 2.3.0 Guest-only 适配记录

## 结论

本地参考包中的 `mtgpu-1.0.0` 是 Linux Host 源码，不是可直接安装的 Linux Guest 驱动；它比社区 Native 2.7.1 核心更适合作为 Guest-only 适配起点，因为配置包含 15 个 OSID、Linux PCI 表含 S3000 `1ed5:0222`、驱动模式枚举含 Guest，模块也声明了 VZ 固件名。针对本机 `6.12.107+deb13-amd64` 的 Guest-only 补丁可从未修改的包源码干净构建。普通 Guest-only 模块已在本机 probe；PVR 在物理 heap 数量检查处退出，没有创建 MTT DRM 节点，不能使用 GPU 加速。另已构建并静态核验一个默认关闭的离线候选，同时修正 heap 数量立即数和 Guest VPU 分支的 FW_MAIN 基址来源；它尚未运行时验证。

后续只读取得官方 Linux 5.2.0 用户态包后，确认它与当前 1.0 Guest KMD 使用的 DRM UAPI 主版本不同（22 对 2），不能直接配套；5.2.0 DKMS 默认也关闭多 OS/Guest 路径。包哈希、UAPI 形态和本机内核构建探测见 [Linux Guest 用户态 ABI 审计](linux-guest-umd-abi.md)。

## 厂商公开支持边界（2026-09-29）

摩尔线程当前公开的 [MT vGPU 2.9.2 白皮书](https://docs.mthreads.com/vgpu/version-2.9.2/vgpu-doc-online/white_paper/) 对 S3000 明确列出 Windows 10 / Windows Server 2019 Guest vGPU 驱动；本次查阅的公开 vGPU 资料没有说明 Linux 图形 Guest 驱动支持或其 Guest ABI。这是公开支持范围的证据，不等于 Linux Guest 在技术上不可能。Linux Guest 适配仍按实验性移植处理，不能从 Windows Guest 行为推定 Linux Guest ABI。另见 [MTGMI 使用手册](https://docs.mthreads.com/gmc/gmc-doc-online/gmi/user_manual/) 中 Linux Guest 条目，它描述的是 `mthreads-gmi` 管理查询功能，不是图形 Guest 驱动接口。

## 本机参考发行包 Guest/Host 分界复核（2026-09-29）

对本机 `downloads/S2000_MT_vGPU_2.3.0.zip` 的 ZIP 目录做了可重复审计：`MT_vGPU_GUEST_v2.3.0` 目录含 `mtkm64.sys`、`mtdispkm64.sys`、`mtvpukm64.sys`，没有 Linux Guest 驱动或 Linux Guest 安装包；Linux `.deb/.rpm` 位于独立的 `MT_vGPU_DKMS_v2.3.0` Host DKMS 目录。审计脚本只读取 ZIP 中央目录，不解包、执行或安装载荷，结果限定于这一个本地 S2000 v2.3.0 ZIP，不推断其它版本是否有 Linux Guest 驱动。可运行 `python3 scripts/verify-vgpu-package-guest-os.py` 复核；本轮 Python 单测共 53 项通过。

同日从随包源码再次干净构建并启用已静态核对的两个后链接修正，产物为 `build/official-vgpu-2.3.0-guest-heapcount-fwbasepubfix-audit-20260929/mtgpu.ko`，SHA-256 `052695c9b38a73b36dce884ca633d647e79f68f2a5f183bbb5382c236d96ca42`。Kbuild 完成 MODPOST 和模块链接；ELF `.modinfo` 含当前 `6.12.107+deb13-amd64` vermagic 与 S3000 `1ed5:0222` PCI alias，离线 stage 仅含模块和随包固件。构建输出有上游 objtool 及零长度 `.note.GNU-stack` 警告，不影响本次链接成功。候选未安装或加载，哈希与静态指令检查不证明运行时 heap 映射、固件启动或加速有效。

针对刚构建的实际 `.ko` 新增 PCI 启动顺序核验：`mtgpu_probe` 先调用 `os_pci_enable_device`，再调用 `os_pci_set_master`，随后才进入 `mtgpu_device_common_init` 和 `mtgpu_vz_init`。因此正式候选已在 PVR/VZ 初始化前开启 Bus Master；这与独立通信探测模块 `kernel/mt_guest_probe.c` 的 Bus Master 实验是不同路径，不应把后者的旧缺陷套到正式模块上。构建脚本现会自动执行 `scripts/verify-guest-probe-pci-master.py`，结果落在 `reports/guest-probe-pci-master-validation.json`。这是 ELF 重定位顺序核验，不证明设备实际接受 DMA。

为防止重复此前 `objcopy --update-section` 造成的 ftrace Oops，又将磁盘上的旧基线模块（SHA-256 `84817b6d8d9e62c8ac650948d1d837267161e4d8e2979ccfca3c5c83170a13cb`）与新候选逐段比对。`.text` 长度相同，唯一差异是两个已知补丁窗口中的 7 个字节；`__mcount_loc`、`.rela__mcount_loc`、`__patchable_function_entries` 及其重定位表完全一致。可运行 `python3 scripts/verify-postlink-ftrace-integrity.py /usr/lib/modules/$(uname -r)/updates/mtgpu.ko build/official-vgpu-2.3.0-guest-heapcount-fwbasepubfix-audit-20260929/mtgpu.ko` 重验。该结果说明没有改动 ftrace 索引/布局，不保证内核运行时加载一定成功。

本机只读状态复核仍见 `/proc/modules` 中旧 `mtgpu` 为 `Loading`、引用数 1；系统模块文件哈希仍是上述旧基线，和离线候选不同。候选没有加载，当前内核的活动模块状态也不适合并行启动第二个同驱动实例。此记录不触碰 PCI、MMIO 或显存；硬件运行结果仍未验证。

本机 Linux Host `.deb` 的固件清单只有 `.vz.win`：S3000 路径使用 `musa.fw.1.0.0.0.vz.win`；1.1/1.2 的内嵌标记分别属于 QY2/PH 家族。源码默认 `mtgpu_load_windows_firmware=true`，所以当前候选默认请求的目标固件已在 stage 和系统固件目录中。没有找到 `.vz.linux` 镜像；若将参数改为 Linux 固件路径，当前参考包无法满足请求。固件家族证据见 [S3000 VZ 固件核验](vz-firmware-family-audit.md)。

## 本机 Linux 用户态组件盘点

本地 `mtgpu-1.0.0` `.deb` 中所有普通文件都位于 `/usr/src/mtgpu-1.0.0` 源码树或 `/usr/lib/firmware/mthreads`；没有 DRM/MUSA 用户态库或可执行程序。系统 `dpkg` 清单也没有 `musa`、`mthreads`、`mtgpu`、`vgpu` 或 PVR 用户态包，`/usr/lib` 与 `/lib` 中没有匹配的 MTT/MUSA 用户态库。也就是说，即使 Guest KMD 后续创建了 DRM 节点，现有本机材料仍不足以提供或验证 MUSA/OpenGL/Vulkan 等 GPU 用户态加速。

摩尔线程公开的 [S3000 Linux Server 驱动文档](https://docs.mthreads.com/driver-linux-server/driver-linux-server-doc-online/MTT_S3000/install_guide/)说明了独立的服务器驱动软件包和安装流程；[Linux Server 产品说明](https://docs.mthreads.com/driver-linux-server/driver-linux-server-doc-online/MTT_S3000/introduction/)将其定位为渲染、编解码、并行计算和 AI 驱动。该包不在当前本机参考材料中，文档也没有把它定义为 vGPU Linux Guest ABI。不能仅因其支持 S3000 就把它的内核模块或用户库与 vGPU 2.3.0 候选混装；需要同一版本的 Guest KMD、固件和用户态闭环。

## 官方 Linux Server 5.1/5.2 包的 Guest/PVZ 静态核对（2026-09-29）

为继续寻找可用的 Linux 端参考，从摩尔线程官方 HTTPS 仓库下载了只用于静态分析的 5.1.0 与 5.2.0 DKMS/固件包，均保存在 `downloads/`：5.1.0 DKMS SHA-256 `0ac25dcc7279e151c590394e70df83e97dcc1bafe288e23b23f77f42ef75037a`、5.1.0 固件 `5504fe3f5b4d33d2f8884c041ca1c4f380d7fdb5b702e459fba2072f9660f7bd`；5.2.0 DKMS `e3f684b1f7582fa0b399a234b945ad4539c565a46399f8294db91368fa32c62b`、5.2.0 固件 `5307e93693c3ab0bac166eef07e1deb9927771f8b0700caddd446a3336ccd183`。哈希与下载服务器和索引包记录一致。另保存了仓库配置 `.deb` 以检查源定义；没有将源加入系统 APT、没有安装包。APT `InRelease` 的 `gpgv` 校验报告 BAD signature，因此没有把仓库索引当作可信签名元数据，也没有据此安装或替换驱动。

5.1.0 和 5.2.0 的源码都把 S3000 PCI ID `0x0222` 映射到 `quyuan1_drvdata`。但两份包内 `inc/config_kernel.h` 都将 `MUSA_NUM_OS_SUPPORTED` 设为 `1`；驱动模式参数、VZ 固件声明以及多 OS 初始化都受 `#if (MUSA_NUM_OS_SUPPORTED > 1)` 控制。因此源码树虽然保留 `MTGPU_DRIVER_MODE_GUEST` 常量和 Guest 代码，这两个实际 DKMS 构建并没有启用 vGPU Guest/VZ 路径，不能作为本机 Guest 驱动替代品。两版固件包都含 `mtfw-gen1.vz.bin`，对应源码 `inc/mt/services/fwload.h` 说明 `.vz` 后缀用于运行在虚拟化 Host 上的 kernel server；它也不能直接替换当前候选使用的 `musa.fw.1.0.0.0.vz.win`。这次盘点没有找到一份经过 ABI/家族核验、可直接替代当前 Guest 镜像的 Linux Guest 固件。

两版的 `struct vgpu_info` 都将版本 1 旧数据区保留为从偏移 `0x28` 开始的 `padding[0xc20]`；本机 2.3.0 Linux ABI 中 `fw_heap_base` 位于 `0x848`，落在这个保留窗口内。共同的 magic/version/OSID/flag/内存长度前缀也一致。这说明 5.1/5.2 头文件没有否定旧 Linux v1 信息页布局；但它只证明结构兼容窗口，不能确认当前 OSID 7 页面字节、flag 或 BAR2 backing。

两版 PVR 源码中的 PVZ 契约说明 Host 入口由摩尔线程预实现，Guest 入口由第三方按具体 Hypervisor 提供；Guest 自己分配 FW heap 时必须实现 `pfnMapDevPhysHeap` / `pfnUnmapDevPhysHeap`，未实现时必须返回 `MTGPU_ERROR_NOT_IMPLEMENTED`。这只是 PVR 框架的接口说明，不代表 5.1/5.2 DKMS 包启用了 Guest provider。不能在本机 Guest 侧凭猜测伪造一个成功 provider，也不能把 PCI 设备地址转换误当作 Guest IPA 到 Host IPA 的映射。当前 2.3.0 Guest-only 分支的静态 premap 路径仍是唯一已有证据的绕行候选，真实 OSID 7 backing 仍未知。

5.1.0 与 5.2.0 的源码都在 `/tmp` 做了本机内核的离线构建探测，没有安装或加载。关闭不相关音频对象后，两版仍遇到相同 Linux 6.12 API 差异：缺少 `PCI_IRQ_LEGACY` 与旧 `follow_pfn()`，`pci_resize_resource()` 参数已变化，平台驱动 `.remove` 回调从 `int` 改为 `void`。所以更新版本也不是可直接替代当前已针对 6.12 构建的 2.3.0 Guest-only 候选；这只是编译兼容性证据，也不证明 5.2.0 Guest 栈与当前虚拟设备 ABI 匹配。官方 [MUSA SDK 5.2 安装指南](https://docs.mthreads.com/musa-sdk/version-5.2.0/install_guide/)说明 APT 方式只面向全新 Ubuntu 22.04 x86_64 Server 环境，并建议彻底移除既有 Linux Driver/MUSA Toolkit；本机 Debian 13 和当前异常活动模块不满足该安装前提。

## 2026-09-29 Guest FW_MAIN 基址候选修订

配套 Linux Host 的 `vgpu_calculate_vpu_mem_size` 输出同时成为 `vm_bar2_actual_mem_size` 与 VVPU 段 `size`，Guest 在 `mtgpu_device_memory_fixup+0x447` 读取 `vgpu_info+0x438` 并将其用于 VPU 内存范围计算。第一版候选把这条 load 错换成 `vgpu_info+0x848` 的固件堆物理基址；这样会把地址当成长度，破坏后续范围运算。该模块 `build/official-vgpu-2.3.0-guest-heapcount-fwbasefix-20260929/mtgpu.ko`（SHA-256 `8b2421a15f8b8f10c8b43852177c306622ef32329b04290f63fe7b08ecd5dcc7`）已撤回，**不可安装或加载**。

修订后的默认关闭候选保留上述 VVPU 大小 load，只在 Guest VZ 平台数据发布时，于 `mtgpu_platform_data_vz_init+0x8d` 通过已经加载并保存的信息页指针读取 `fw_heap_base+0x848`，并写入 `fw_heap_card_base`。对应机器码是 `48 8b 80 48 08 00 00 90`，紧接着写到平台字段 `+0x50`；原始 `mtgpu_device_memory_fixup+0x447` 指令 `48 8b 8a 38 04 00 00` 完整保留。合并四堆计数修正后的离线产物：

`build/official-vgpu-2.3.0-guest-heapcount-fwbasepubfix-20260929/mtgpu.ko`
SHA-256：`d0ba818d96dba200e433c416cbcf918862d0a920859338760e4b09abc1f7002b`

补丁脚本现在还要求 Guest-mode 分支、此前从设备结构取出并发布到 `pdata+0x70` 的信息页指针两条指令保持不变；这避免仅凭同一构建标签和一个目标指令就改写已漂移的数据流。34 项 Python 单测和候选二进制上下文核验通过。静态核对确认 heap 基数立即数为 5、当前内核 vermagic、S3000 PCI alias `1ed5:0222` 和 ELF 检查通过，stage 含三个 Windows VZ 固件版本；Host/Guest 地址路径核验也通过。模块只构建在隔离目录，未安装、未加载，也未访问 PCI/GPU。它仍是待运行态验证的候选，不代表适配已成功。

OSID 7 Linux v1 原始信息页字节仍缺失，无法实测 Host 下发的 PAddr。Host 公式结合 Windows OSID 4 参考记录推得 OSID 7 的 FW_MAIN GPU PAddr 候选为 `0x7737ef000`、BAR2 aperture offset 候选为 `0x40800000`；这只确认每 OSID 的相对步进一致。PVR `FW_PREMAP7` 设备地址 `0xe1c3800000` 的运行时 backing 也未证明，因而不能声称固件启动或 GPU 加速正常。

源码包将 Host 的 mdev/VFIO 创建接口与 Guest 设备驱动编译在同一模块中。Linux 6.12 已不再提供该版本使用的 `mdev_parent_ops` API，旧 KVM/VFIO Host 辅助接口也无法按原样编译。补丁用 `MTGPU_GUEST_ONLY_BUILD` 将模块默认驱动模式设为 Guest，并将 Host-only 的 mdev 创建、VFIO 通知和 KVM Guest-memory 查询接口改为不可用返回。该变体不适用于 Native 或 Host vGPU 管理。

对预编译核心的反汇编确认了这些桩不会走入正常 Guest probe：模块初始化变量 `mtgpu_driver_mode` 被固定为 `1`，且不再导出成可覆盖的模块参数；`mtgpu_probe` 在模式为 `1` 时跳过 `mtgpu_ipc_add_device`。`mtgpu_vz_init` 入口调用 `mtgpu_get_driver_mode()` 后，Guest 模式直接返回成功，Host 的 SR-IOV/mdev 初始化以及 `mtgpu_mdev_init` 调用位于模式为 `0` 的分支。`mtgpu_mdev_init` 是核心中唯一调用 `os_mdev_register_device` 的入口。因此 Host API 桩是为链接保留，不是 Guest probe 的替代实现。

这些反汇编地址对应本包 `MT_BUILD_TAG=5c6c275` 的 x86-64 核心：`mtgpu_probe` 的 `0x43cc7` 比较 Guest 模式并跳过 IPC 调用；`mtgpu_vz_init` 在 `0x52a78` 读取模式、`0x52a7d` 判断非 Host 并于 `0x52a90` 返回，而 Host 专用 `mtgpu_mdev_init` 调用位于 `0x52ab3`。这是静态控制流证据，不替代本机 probe 实测。

## Windows RM heap profile 与 PCI 资源映射审计（2026-09-29）

Windows 反编译提供了有限旁证：mtkm64.sys 的 gpu_device RM 构造函数 FUN_14003c4d0 对输入 selector 0x0200 选择 4 个 heap，和 Linux 源码把 S3000 0x0222 掩码为 QUYUAN1 family 0x0200 的路径一致。该 selector 的字段语义尚未证明等同于 Linux family mask，Windows RM 与 Linux PVR 核心也不是同一实现；所以它只支持四堆候选，不把候选提升为已验证修复。

同一 Windows RM 的 FUN_14003d038 将显存 segment 登记到本地 gpu phys heap allocator；FUN_14003ca64 和 FUN_14003abe4 通过 MmMapIoSpace 映射 PCI aperture。它们是本地 BAR/资源描述符映射路径，没有 Guest IPA 到 Host IPA 转换或 PVZ Host 接收端，不能当作缺失的 Linux VMM 回调。反编译、受限 Unicorn 分支核验与 xref 证据见 [Windows RM heap 与 PCI 资源映射审计](windows-resource-mapping-audit.md) 和机器可读结果 windows-gpu-device-heap-count.json。

同日只读运行态复核：`/proc/modules` 中 `mtgpu` 为 `Loading`、引用数 1；PCI 设备 `0000:00:0e.0` 没有 driver symlink，PCI COMMAND 为 `0x0003`（I/O 与 Memory Enable，Bus Master 关闭）。四堆候选仍是离线文件，SHA-256 为 `6cb4f0c1c731120e8e49acfb68dc578052813aa28840354840f7f20cc812e23c`；本轮未对当前模块状态做恢复或叠加载试验。

## 2026-09-29 干净源码重建与离线验证

再次从随包源码创建隔离构建目录并运行 `scripts/build-official-vgpu-guest.sh`，开启可选 Local Guest heap-count 修正。Kbuild 完成 `MODPOST` 和链接；`SysDevInit+0x92` 从 `41 83 c5 06` 改为 `41 83 c5 05`。新候选位于
`build/official-vgpu-2.3.0-guest-heapcountfix-20260929/mtgpu.ko`，SHA-256 为
`8bf4a81b4219c5c89c0bd2b0382d3469859f716036c8e85b6981b116a18e8a7f`。`readelf --lint`、当前内核 vermagic、`1ed5:0222` PCI alias 与指令复核均通过；候选只在本地 build/stage 目录，未安装或加载。

同轮 `python3 -m unittest discover -s tests` 的 18 项测试通过；当时的 `scripts/verify-runtime-integration.py` 尚因旧测试夹具未跟上新接口而失败。构建中仍有参考源码和预编译核心带来的 compiler/objtool 警告，但无编译或 MODPOST 失败。此离线测试状态已在本轮更新如下。

## 2026-09-29 集成夹具对齐与完整离线回归

修复了用户态 GEM/TQX/启动 BO 测试与现有实现之间的三类过期假设：GEM 测试现以 test-only RAM backing 和 boot-BO 边界建模，直接编译真实 BO、GPU VM、GEM 及 TQX 工作准备代码；封存且空闲的 VM 允许更新已有映射内的对象内容，但页表保持封存；已完整映射的启动共享集合允许重复绑定并幂等返回。另将 `WARN_ON` 的用户态替身改成可用于表达式的整数返回宏。

验证命令 `python3 scripts/verify-runtime-integration.py` 现已全部通过：15 个 C/RAM 集成测试、`W=1` 主模块构建无告警，以及当前模块与保留模块备份的 `mt_guest` ABI 对照；另有 `python3 -m unittest discover -s tests` 的 18 项测试通过。机器可读结果写入 `reports/runtime-integration-build.json`。它只确认 CPU/RAM 模型、所有权逻辑和构建；主模块未加载、没有 PCI/GPU 写入，也没有验证实际 Guest/Host PVZ 映射、连接、上下文发布或 GPU 渲染。

本次仍没有硬件加速验证。运行态 `/proc/modules` 的 `mtgpu` 异常 `Loading` 状态和设备未绑定不允许安全叠加载；继续运行候选仍需先有正常模块生命周期。heap-count 依据由 Windows `0x0200` selector 的四堆构造路径加强，但这一字段关系仍是跨驱动旁证。即使初始化越过 heap 数量检查，PVZ Guest `MapDevPhysHeap` / `UnmapDevPhysHeap` 仍是 `NOT_IMPLEMENTED`，缺少 Host 映射服务；本轮没有伪造成功返回或猜测 hypercall ABI。

## 编译证据

- 目标内核：`6.12.107+deb13-amd64`
- 补丁应用：从 `src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0` 干净复制后 `patch -p1` 成功
- 构建：Kbuild 完成 `MODPOST` 和 `LD [M]`，产生约 20 MB 的 `mtgpu.ko`
- 模块元数据包含 PCI alias `1ed5:0222`，依赖项不再包含 `kvm` 或 `vfio`
- 编译出的全局 `mtgpu_driver_mode` 初值为 `1`，模块元数据中没有 `mtgpu_driver_mode` 参数
- 构建期间 objtool 对预编译核心报告间接函数指针和 mitigation-return 警告；未出现编译或 modpost 错误

复现命令：

```sh
./scripts/build-official-vgpu-guest.sh
```

脚本只创建隔离构建目录、编译模块，并把模块与参考包固件整理到该目录的 `stage/` 下；不安装、不加载、不绑定设备。普通构建结果保存在 `build/official-vgpu-2.3.0-guest-modefix/mtgpu.ko`。针对下述 heap 数量偏差，另有默认关闭的实验修正：

```sh
MTGPU_GUEST_PATCH_PHYSHEAP_COUNT=1 \
MTGPU_GUEST_BUILD_DIR="$PWD/build/official-vgpu-2.3.0-guest-heapcountfix-20260928" \
./scripts/build-official-vgpu-guest.sh
```

FW_MAIN 基址来源候选（与 heap 数量修正合并）的复现命令：

```sh
MTGPU_GUEST_PATCH_PHYSHEAP_COUNT=1 \
MTGPU_GUEST_PATCH_FW_HEAP_BASE=1 \
MTGPU_GUEST_BUILD_DIR="$PWD/build/official-vgpu-2.3.0-guest-heapcount-fwbasepubfix-20260929" \
./scripts/build-official-vgpu-guest.sh
```

FW heap 候选脚本按 ELF 符号定位，将 `mtgpu_platform_data_vz_init+0x8d` 的 Guest 发布 load 改为通过信息页指针读取 `[vgpu_info+0x848]`，并在修改前后检查 `mtgpu_device_memory_fixup+0x447` 的 `[rdx+0x438]` VVPU 大小 load 未变。构建标记、架构、原指令或目标符号任一不符都会拒绝修改。开关默认关闭，不会影响普通构建。旧 `heapcount-fwbasefix` 产物因错误替换大小 load 已撤回，不能使用。

修正脚本只对链接后的 x86-64 `mtgpu.ko` 原位改写 `SysDevInit+0x92` 指令中的 1 个立即数字节：`41 83 c5 06` 改为 `41 83 c5 05`。它验证构建标记、ELF 符号和原指令，不改变 ELF 节长度、重定位或 ftrace 表。新增的分支审计显示：Local Guest 在本机观测为实际 4、期望 5；HOST 内存模式 Guest 的静态路径同样生成 4、期望 5；Hybrid Guest 的最终计数是 `mtgpu_vz_data.osid_count + 2`，而候选期望数为 5，只适用于部分拓扑，本机值尚未读取。该立即数也作用于非 Guest 控制流，因此仍保持默认关闭。详细偏移、计数推导和限制见 [Guest PhysHeap 控制流审计](guest-physheap-control-flow.md)。

2026-09-28 的新鲜构建目录为 `build/official-vgpu-2.3.0-guest-heapcountfix-20260928/`，候选模块 SHA-256 为 `6cb4f0c1c731120e8e49acfb68dc578052813aa28840354840f7f20cc812e23c`。`readelf --lint` 通过，反汇编确认立即数为 5，模块 `vermagic` 与当前内核一致，PCI alias 包含 `1ed5:0222`，stage 中带有 3 个 `.vz.win` 文件；未安装、未加载该候选。

## 本机运行证据与 heap 阻塞

加载 Guest AppHint 修正版本后，PCI probe 匹配 `0000:00:0e.0 [1ed5:0222]`，PVR 日志显示 `Driver mode: Guest, FW type: Windows`。诊断日志从传给 PVR 的平台数据读到 `mem_mode=1`、GPU 内存基址 `0x800000000`、大小 `0x43000000`，两个内存段分别为 `[0x100000000, 0x400000000)` 和 `[0, 0xfffef000)`。Guest 信息页 magic 为 `0xaa557491`，当前页报告 OSID 7、1 GiB VM 内存和 3 个 GPU 段。PVR 随后报 `PhysHeapsInit: Failed to init heaps [644]`，`SysDevInit` 失败，DRM 组件解绑；没有 `/dev/dri/card1` 或 MTT render node。

对 `5c6c275` 核心的反汇编显示：本机 Local Guest 路径分配并构造 4 个 heap 描述符，但期望数仍是 5。可选 Guest-only 实验补丁将 `SysDevInit` 中的 heap 基数从 6 调为 5；核心现有 Guest 分支再减 1 后，Local 和 HOST 内存模式 Guest 的静态期望数为 4。Hybrid 分支依赖 `mtgpu_vz_data.osid_count`，该补丁不能视作 Hybrid 通用修复。对普通最终 `.ko` 的原位后链接修正做了字节级验证，干净构建成功；没有运行时验证，因此不能据此声称 PVR 初始化已通过。

继续静态检查了同一官方 2.3.0 core 的 `PvzConnectionInit`：它调用 `VMMCreatePvzConnection`，后者收到非空输出地址时存入本地 VMM dispatch-table 地址并返回 0；调用者随后确认该槽非空、设置初始化标记并返回成功。`0x14` 固定返回是另一个社区 Native 2.7.1 core 的结论，不适用于此 Guest-only 候选。这个静态路径不能证明 heap-count 修正后 probe 会到达或成功执行连接初始化；真实运行仍被当前内存中的异常模块状态阻断。

还发现 heap-count 之外存在条件性 PVZ 依赖：官方 2.3.0 core 的 VMM table 中，Guest client 的 `pfnMapDevPhysHeap` / `pfnUnmapDevPhysHeap` 指向 `StubVMMMapDevPhysHeap` / `StubVMMUnmapDevPhysHeap`，两者都固定返回 `0x0a`（`PVRSRV_ERROR_NOT_IMPLEMENTED`）。`vmm_impl.h` 说明动态 Guest heap 分配需要具体 VMM 的 hypercall 回调。但新追踪的 `RGXInitCreateFWKernelMemoryContext` Guest mode=1 分支会把 FW_MAIN/FW_CONFIG 标为 premap，并跳过同一函数内 Host 侧的 `RGXFwRawHeapAllocMap` 循环。因此这个 stub 目前不能断定是 static Guest 启动的必经阻塞；其它动态回调、Guest premap 的 BAR2 backing 和 OSID 地址转换仍未验证。无需猜测返回成功，也不能据此认定硬件已能启动。细节见 [静态 Guest FW premap 控制流](static-guest-fw-premap.md)。

曾考虑通过翻倍 Guest 布尔值修正数量，但复核发现 `EAX` 高位未清零，该方案已废弃，没有用于最终模块。

此前试验曾用 `objcopy --update-section` 直接改预编译核心对象；加载该实验模块触发 ftrace 校验失败和内核 Oops，模块初始化停在 `Loading`，PCI 设备当前未绑定。磁盘上的 `/lib/modules/6.12.107+deb13-amd64/updates/mtgpu.ko` 仍是干净的 Guest AppHint 构建，SHA-256 为 `84817b6d8d9e62c8ac650948d1d837267161e4d8e2979ccfca3c5c83170a13cb`。当前内存中仍保留异常模块状态；本轮只做源码和离线构建，没有触碰该状态、操作宿主或重启。

## 固件限制与下一步

模块声明了 `musa.fw.1.0.0.0.vz.win` 和 `musa.fw.1.0.0.0.vz.linux`，但本地参考包的固件目录实际只包含 `.vz.win` 版本。现已直接检查当前 Guest 候选的 `mtgpu_core.o_binary`：`mtgpu_vgpu_is_win_fw_mode` 读取 `mtgpu_load_windows_firmware`，`mtgpu_is_win_fw_mode` 转发该结果，`RGXLoadAndGetFWData` 按结果选择 `.vz.win` / `.vz.linux` 后缀并调用 `OSLoadFirmware`。源码默认值为 `true`，所以默认分支会请求 `musa.fw.1.0.0.0.vz.win`，候选模块的 `.modinfo` 声明该固件，离线 staging 中也存在 S3000 对应的 1.0.0.0 文件；反向 Linux 固件分支仍无法满足。可运行 `python3 scripts/verify-guest-fw-selector.py` 重验源码参数、候选二进制重定位/分支、模块元数据和 staged 固件哈希。这个闭环只确认默认文件选择与 staging 一致，不证明固件被加载、启动或与当前虚拟设备的运行时 ABI 完全匹配。

本机系统曾安装参考包提供的固件，但 heap 检查失败发生在固件启动之前。包内只有 `.vz.win`，没有 `.vz.linux`，当前 Guest 路径选择 Windows 固件。堆数候选已完整重建并做 ELF/元数据静态核验。静态 Guest premap 分支绕过一个 Host raw-heap map 循环，但目前没有实机证据证明 Guest OSID 槽已正确 backing；若运行时走动态 heap map，仍需要匹配的 `MapDevPhysHeap` / `UnmapDevPhysHeap` provider。Guest 的虚拟化标识不能唯一确定其 ABI。Host mdev 桩只为 Guest-only 模块保留链接；若要支持 Host vGPU 创建，必须针对新 VFIO device/mdev API 单独移植。

### PVZ 映射 provider 的本机审计（2026-09-28）

继续检查了现有 Guest↔Host 通道和 Windows 参考驱动，没有找到可直接复用的 PVZ heap 映射实现：`kernel/mt_host_query.h` 处理 type=1、subtype=0/1/2 统计查询，其他 RPC 会被拒绝并保留在队列中；它没有 `MapDevPhysHeap` 或 `UnmapDevPhysHeap` 操作。Windows `mtkm64.sys` 通过私有 IOCTL `0x222408` / `0x222404` 从 `mtdispkm64.sys` 获取 0x40 / 0x228 字节的 MTT 函数表。这证明了驱动间的私有调用接口，但当前反编译结果不能把它们对应到 Imagination PVZ 回调。三个相关 Windows 内核驱动的导入表没有 Hyper-V hypercall helper，反汇编也未发现 `vmcall` / `vmmcall` 指令；这不排除由其他组件代理，但不能据此构造 Linux 回调。

补充复核 Windows RPC handler `14002729c`：Windows 的 type=1/subtype=2 返回 `EscapeSetGpuUtilStat` 写入的 adapter `+0x1da8`，值为零时回退到 `+0x1d90`。Linux RPC helper 已实现可注入的第二统计值；由于没有同源计数器且工作提交仍关闭，当前 Guest 运行态传零值以消费 idle 查询，不能代表 GPU 忙时遥测。Windows 的 type=2/subtype=2 分支只在 payload 低字节为 4/5 时调用 MTT 私有 connector `14002b260`；追到的两个回调分别返回零后报错、直接返回 `0xfffffffc`，不是 PVZ 映射实现。此发现补全了 RPC 差异清单，但没有提供可用的 `MapDevPhysHeap` provider。

本机 CPUID 显示 Hyper-V 签名 `Microsoft Hv`、接口签名 `Hv#1`，Hyper-V 功能叶的 hypercall 可用位也已置位；虚拟化环境同时被识别为 QEMU，且没有 VMBus 设备。这只证明通用 Hyper-V hypercall 通道可用，不提供 MTT PVZ 的函数号、参数布局、返回码或 Host 端映射语义。Linux Guest 头文件里的 `fw_heap_base` / `mmu_heap_base` 属于旧版信息页布局；本机 Windows 参考探测记录的是另一种 version-2 布局，不能把其中的 BAR2 段地址直接当成 PVZ 已完成的 Host IPA 映射。

后续对同版本 Linux Host 的 `vgpu_access_pci_bar1_region` 静态核对确认，它构造 magic `0xaa557491`、version 1 的旧式信息页，并将固件堆 base/size 写在 `0x848/0x850`。因此 Linux 2.3.0 Guest 使用的旧式布局与配套 Linux Host 彼此一致；本机 Windows v2 探测页不可替代该 ABI，也不是 Linux Guest OSID 7 的原始页。现存 Guest 日志没有保留 `flag`、VVPU 段字段和固件槽的原始字节，所以 FW_MAIN 真实基址来源仍需 OSID 7 的运行态页摘录或对应 Host 状态才能确认。详见 [BAR2 固件堆路径分析](windows-fw-heap-ring-investigation.md)。

再直接核对了 2026-09-28 Guest 候选 `mtgpu.ko`：`gsStubVmmPvz` 的 ELF 重定位将 Guest client 的 map/unmap 槽连到 `StubVMMMapDevPhysHeap` / `StubVMMUnmapDevPhysHeap`，将 server 侧 map/unmap 槽连到 `PvzServerMapDevPhysHeap` / `PvzServerUnmapDevPhysHeap`。这两个 server 函数在日志后直接返回 `0x152`。按随包 `pvrsrv_errors.h` 的枚举顺序（`PVRSRV_OK` 为 0），`0x152` 对应 `PVRSRV_ERROR_INVALID_PVZ_CONFIG`；两个 `OnVmOnline/Offline` 入口则会转调实际的 `PvzOnVmOnline/Offline`。这说明随包核心没有为这两项 heap 映射请求提供可用的 PVZ 配置/实现。此结论限定于随包 Linux 核心；外部 MTT Host 组件是否另有代理实现，现有本机材料无法证明。

2026-09-29 复核了新候选中 PVZ 构造函数的 ELF 重定位：`VMMCreatePvzConnection` 的 out-pointer 实际被重定位为本地 `gsStubVmmPvz` 表地址，`PvzConnectionInit` 可成功完成本地初始化；表中的 Guest client map/unmap 仍指向 `NOT_IMPLEMENTED` stub，server map/unmap 返回 `INVALID_PVZ_CONFIG`。因此本地初始化成功不等于存在跨 VM transport。Guest 固件静态 premap 分支会跳过其函数内的动态 raw-heap map 循环，但其它调用路径和 OSID 7 Host backing 仍未验证。纠正后的 relocation-aware 证据见 [PVZ ABI 与 Host 半边对照](pvz-map-abi-and-host-half.md)。

若实际运行路径触发 PVZ map/unmap，Guest 侧必须保留 `StubVMMMapDevPhysHeap` / `StubVMMUnmapDevPhysHeap` 的失败语义（`PVRSRV_ERROR_NOT_IMPLEMENTED`），直到找到匹配 provider。把返回值改成成功、盲发未知 Hyper-V hypercall，或仅依据 `GuestDevicePAddrToHostDevicePAddr` 做地址换算，都不能完成 PVZ 的 Guest IPA 到 Host IPA 映射；其中地址换算函数由 `DevPhysAddr2DmaAddr` 用于设备 DMA 地址转换，不能替代映射请求。static carveout Guest 分支目前已确认绕过一个 Host raw-heap map 循环，但这不排除其它 PVZ 调用点。动态路径要补齐仍需要匹配平台的 Guest PVZ provider ABI 和 Host 接收端。本轮没有触碰当前内存中的模块、设备绑定或宿主状态。

进一步对照本地 Native 2.7.1 core，发现其 server map 会按 OSID 检查 VM 在线并调用 `RGXFwRawHeapAllocMap`，unmap 则更新固件状态并调用 `RGXFwRawHeapUnmapFree`；但同一 core 的 Guest/client map/unmap 仍是 `PVRSRV_ERROR_NOT_IMPLEMENTED` stub。版本差异也落实到了分配器：官方 2.3.0 raw-heap helper 严格接受 8 MiB、非零 PAddr 和 OSID<15；Native 2.7.1 helper 严格接受 256 MiB 和 OSID=0，且其构建配置只支持一个 OS。因此 Native core 不能当作目标 S3000 Guest 的兼容 Host provider。官方 2.3.0 候选的 server map 入口仍返回 `INVALID_PVZ_CONFIG`，并且两份 core 都没有跨 VM transport。细节和可复核反汇编见 [PVZ ABI 与 Host 半边对照](pvz-map-abi-and-host-half.md)。

2026-09-29 又对官方 2.3.0 core 的直接 ELF 调用重定位和随包 C/H/S/Makefile 做了全量扫描：raw FW heap map 只有一个直接调用点，位于 `RGXInitCreateFWKernelMemoryContext` 的 Host 分支；两个 raw unmap 点分别是 setup 失败清理和 Host deinit，Guest deinit 会跳过 Host unmap。PVZ client map/unmap wrapper 没有直接重定位调用者，随包源码也没有引用。由此没有证据表明缺失的 PVZ stub 是当前 static Guest FW 初始化的必经阻塞；这不能排除不透明的间接/外部调用，也不能证明 E1C premap 与 Host BAR2 OSID 槽已正确映射或 backing。可运行 `python3 scripts/verify-pvz-call-path.py` 与 `python3 -m unittest discover -s tests -p 'test_*.py'` 重验。详细边界见 [静态 Guest FW premap 控制流](static-guest-fw-premap.md)。

同一轮继续核对 FW heap 子堆与 OSID 槽：PVR 公开头文件声明 `FW_CONFIG` 是 `FW_MAIN` 的子堆，`rgxfwutils.h` 将 CONFIG 分配路由到 `psFirmwareConfigHeap`，Guest 固件上下文也分别将 MAIN/CONFIG 标记为 premap。host-slot 校验器现验证 FW_CONFIG usage 值 `0x100` 及该子堆关系；因此基址候选覆盖父堆坐标，不需要另造 config 地址。另确认配置支持 15 个 OSID，`FW_PREMAP7` heap ID 18 映射到 raw heap 数组索引 7，蓝图地址为 `0xe1c3800000`。按 OSID 4 参考记录推算的 Host OSID 7 BAR2 offset `0x40800000` 与 GPU/card PAddr `0x7737ef000` 都呈现 `0x1800000` 步进，但地址域转换和实际 backing 仍未证明；相关检验见 [静态 Guest FW premap 控制流](static-guest-fw-premap.md)。本轮 Python 单测共 44 项通过。

### 2026-09-29 双版本信息页离线适配候选

保存的 `reports/device-info.bin` 是 Windows Guest 参考探测得到的 version 2、OSID 4 页面。按 Linux v1 的 VVPU/FW/MMU 字段偏移检查，它在 `+0x438/+0x848/+0x850/+0x858/+0x860` 处均读为 0；V2 固件范围实际位于段表记录中（`flags&4`）。因此此前只读取 Linux v1 `fw_heap_base` 的候选不能兼容这份 V2 页面。这个发现只描述该保存样本，不说明本机 Linux Guest 当前收到 V2。

新增 `patches/mtgpu_vgpu_info_compat.c` 与 `scripts/patch-guest-vgpu-info-compat.py`，为 Linux v1 与保存样本所示 V2 格式提供严格基址解析：v1 校验 magic/version、非零基址和至少 8 MiB 大小后读取 `+0x848/+0x850`；v2 检查 `+0xc50` 段数上限，从 `+0x28` 按 24 字节步长查找唯一的 `flags&4` 记录，并要求有效基址和至少 8 MiB 大小。重复、缺失、越界或无效记录返回 0；代码不将 GPU/card 地址转换为 BAR2 offset 或 Guest/Host IPA。

后链接补丁把 `mtgpu_platform_data_vz_init+0x8d` 的 Guest 基址读取改为调用该解析器；把 `mtgpu_device_memory_fixup+0x447` 的 VVPU 大小读取改为公共前缀 `vm_bar2_actual_mem_size+0x20`。随包 Linux Host v1 的 `vgpu_calculate_vpu_mem_size` 会把相同大小写入 `+0x20` 与 VVPU `.size`，故保留原 v1 数值语义，同时避免 V2 段表内容被当作大小。反汇编另发现 `mtgpu_device_memory_fixup` 在 flag bit 0 未置位时会读取旧版 `fw_heap_size+0x850` 三次；这些位置改为 `0x800000`。这是与该构建匹配的固定值：Host 私有 vGPU 记录和 BAR2 固件槽均为 8 MiB，`MTGPU_FW_HEAP_SIZE=(1<<RGX_FW_HEAP_SHIFT)` 且本包 `RGX_FW_HEAP_SHIFT=23`，PVR FW_MAIN 描述符也为 8 MiB。这样保留 Linux v1 的字段值，并覆盖 V2 页面缺少 `+0x850` 字段时的非 VPU 分支。若 Host 版本或堆配置改变，这个固定值假设需重新核实。构建脚本默认不应用该后链接补丁；启用方式是：

```sh
MTGPU_GUEST_BUILD_DIR="$PWD/build/guest-vgpu-info-compat" \
MTGPU_GUEST_PATCH_PHYSHEAP_COUNT=1 \
MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT=1 \
scripts/build-official-vgpu-guest.sh
```

本机 6.12 内核的离线候选为 `build/official-vgpu-2.3.0-guest-vgpu-info-v2-compat-audit4-20260929/mtgpu.ko`，SHA-256 `86a3d14ac6b175c3f2ce63cbd322ad4f308f58ea55c1b1846ef0b597679caec2`。65 项 Python 单测通过；同一次 Kbuild 链接前后的 ELF 对照确认只有 FW helper call、BAR2 size load、三处固定 FW heap size 和已知 heap-count 修正的 28 个字节变化，四个 ftrace/重定位表完全相同；Guest PCI enable/master/init 顺序静态检查通过。构建只放在 `build/` 和离线 staging 中，没有安装或加载模块，也未读 BAR/MMIO。页面解析正确只由 Linux v1 合成页和保存的 Windows v2 样本证明；Linux Guest 的实时页版本、基址地址域、OSID 7 heap backing、固件启动和实际加速仍未验证。

### 2026-09-29 V1/V2 设备地址转换离线候选

继续扫官方 2.3.0 core 后发现 `GuestDevicePAddrToHostDevicePAddr` 会从信息页读取 Linux v1 的 VVPU/VGPU 段表及 FW/MMU heap 字段；它被 `_SetupPxE`、`MMU_MapPages`、`MMU_UnmapPages`、`MMU_MapPMRFast`、`MMU_AcquireBaseAddr` 和 `DevPhysAddr2DmaAddr` 等路径直接调用。若信息页是 V2，V1 解释会把 V2 固定字段/段记录错当成计数、大小和基址。

Windows Guest 的 `mtkm64.sys` `FUN_140027ab4` 给出了可核对的 V2 规则：先用 header flag `0x10` 将 PB free-list（`+0xc90/+0xc98`）作为前缀，再按 `+0xc50` 数量遍历 `+0x28` 开始的 24 字节记录，仅把 `flags&7` 的段计入连续虚拟偏移，命中后返回段设备物理基址加段内偏移。保存页面的边界锚点为：PB `0→0x36000000`、普通段起点 `0x200000→0x605000000`、私有段起点 `0x5200000→0x13a000000`、固件段起点 `0x3f000000→0x771fef000`，BAR2 范围终点 `0x43000000` 被拒绝。该规则与 Linux V1 函数按累加段大小解释普通 GDPA 的坐标方式一致；但 Linux OSID 7 的运行时输入仍未取得。

新增 `patches/mtgpu_vgpu_addr_compat.c` 与 `scripts/prepare-guest-vgpu-addr-compat.py`：在只读复制的构建目录中把预编译 V1 函数符号改名为私有 fallback，再链接版本调度器。调度器沿原函数同一条指针链读取信息页，V1 原样转调保留的函数，V2 调用新增段表换算器，未知版本、坏页、超范围和未映射偏移返回 0。V2 helper 会先验证整张已映射段表及地址范围，再产出单个地址；它不伪造 PVZ map/unmap，也不把 V2 的 GPU 地址等同于 Host IPA。构建脚本只有在同时启用 `MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT=1` 与 `MTGPU_GUEST_PATCH_VGPU_ADDR_COMPAT=1` 时才修改构建副本，默认配置不变。

已确认的 Windows V2 翻译函数把绝对 GDPA 与 BAR2 GPU 窗口 `[base, base+size)` 比较，再减去 BAR2 基址后应用 PB/段表。Linux 原 V1 函数也从其设备状态对象的 `+0x48/+0x40` 读取该 BAR2 GPU 基址和长度，信息页指针在同一状态的 `+0x70`。因此 dispatcher 需接收绝对地址；这也暴露了早期 audit5c helper 虽与 Windows 段表算法逐项相同，却只按相对 offset 测试、没有覆盖 Linux wrapper 的输入坐标。audit5c 不能作为可用候选，下面的 audit6 已改为沿 V1 同一状态链读取这些字段，要求状态窗口长度等于信息页 `+0x20`，检查加法溢出及 GDPA 落在窗口内，然后用 `GDPA - BAR2_base` 做 V2 映射。这个 Linux 输入契约是对当前核心反汇编的静态推断，尚无 OSID 7 运行时结构作验证。

### 2026-09-29 V2 GDPA 窗口长度交叉核验

复核 audit13 时重新检查了上述长度相等条件。官方 core 的原始 `GuestDevicePAddrToHostDevicePAddr` 反汇编在 `0xe9d25` 起沿 `[psDevConfig+0x78] -> [+0x08]` 取得状态对象；随后从该对象 `+0x48` 读窗口基址、`+0x40` 读长度，并以 `base <= GDPA < base+size` 决定 BAR2 翻译路径；信息页指针则从同一对象 `+0x70` 读取。新 dispatcher 沿相同链读取这三个字段。因此这里的长度是 core 的 Guest GDPA 虚拟窗口长度，不是 Linux PCI 资源表中 16 GiB 的物理 BAR2 aperture。

保存的 Windows V2/OSID 4 页面 `+0x20` 为 `0x43000000`；同一 Guest 的只读 PVR 初始化诊断也记录 GPU 内存基址 `0x800000000`、大小 `0x43000000`。这与原翻译器所需的虚拟窗口相符，而 Windows V2 VPU shared segment 的 BAR2 地址验证仍单独使用 PCI aperture `0x400000000`。新增 `test_dispatcher_uses_guest_virtual_window_not_physical_pci_aperture` 检查两个尺寸不能混用：使用保存 V2 页和 `0x43000000` 能映射固件段，误传 `0x400000000` 则失败关闭。此用例验证 dispatcher/转换 helper 的静态 ABI 约束，不证明 Linux OSID 7 的实际状态对象运行时值；该值仍待运行态确认。

audit6 候选位于 `build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-compat-audit6-20260929/mtgpu.ko`，SHA-256 `81d6c8a4ed546ed30c6dec96fa01e5cb1ba8bed734c58c8e8fe627cba22f1b8a`。链接审计确认原 V1 函数体地址 `0x11a5f0`、长度 `0x12c` 保持不变，7 个核心重定位仍调用 V1 fallback；V2 dispatcher/helper 单独链接。11 项针对性解析、边界和绝对地址 dispatch 测试以及全套 69 项 Python 测试通过；`scripts/verify-v2-gdpa-compat.py` 现在将绝对 `BAR2_base + offset` 同时送入 C helper 与原 Windows `FUN_140027ab4`，142 个边界/随机点全部一致。`-Wall -Wextra -Werror` 用户态编译、`6.12.107+deb13-amd64` vermagic、S3000 PCI alias、PCI probe 顺序、链接符号/7 个调用点和同次构建 ftrace 完整性检查通过，staging 文件与候选 SHA-256 相同。Kbuild 仍打印既有 precompiled core `objtool` ENDBR 数据重定位警告；没有新增 C 编译错误。构建与 staging 均离线，没有安装或加载模块，也没有访问 BAR/MMIO。

audit6 的堆数修改仍作用于共享 `add $6,%r13d`，会改变非 Guest 控制流，现已由 audit8 取代。audit8 将减一移到 Guest 模式条件跳转：Guest 分支跳入 `SysDevInit+0x8b6` 的原 10 字节对齐 NOP，执行 `sub $1,%r13d` 后回到原 Guest 入口；共享 `add $6` 保持不变。候选位于 `build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-compat-audit8-20260929/mtgpu.ko`，SHA-256 `ab1f47c544215d98d524c17628aa729549164c50365bd524b786820fb983e4fa`。全套 73 项 Python 测试、Guest trampoline 分支和目标核验、V1 函数体及 7 个调用点链接审计、S3000 alias、`6.12.107+deb13-amd64` vermagic、PCI probe 顺序和同构建 ftrace 检查通过；39 个后链接字节变化均落在受控位置，四个 ftrace/重定位表未变，staging 哈希相同。Kbuild 仍有预编译 core 的既有 `objtool` ENDBR 数据重定位警告，没有新增 C 编译错误。模块只在离线 staging 中构建，未安装、未加载，也未访问 BAR/MMIO。

V2 页面以外的 MMU/系统内存地址类别在该段表 helper 中会失败关闭；也尚未证明本机 Linux Guest 收到 V2 页、状态链字段有效，或传入该函数的所有 GDPA 都属于 BAR2 窗口。Guest heap-count 修正仍仅由静态计数推导支持；OSID 7 Host backing、PVZ 映射、固件启动和加速结果都未知。离线重建命令：

```sh
MTGPU_GUEST_BUILD_DIR="$PWD/build/guest-vgpu-v2-address-compat-rebuild" \
MTGPU_GUEST_PATCH_PHYSHEAP_COUNT=1 \
MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT=1 \
MTGPU_GUEST_PATCH_VGPU_ADDR_COMPAT=1 \
scripts/build-official-vgpu-guest.sh
```

### S3000 固件家族与本机 PCI 端点复核（2026-09-29）

新增可重复的离线检查，确认 `1ed5:0222` 在官方驱动的 3D/VGA PCI 表中都走 `quyuan1_drvdata`，驱动只声明 `1.0.0.0.vz.{linux,win}` 且默认选择 Windows 固件。目录中的 1.1/1.2 Windows VZ 镜像分别带有 QY2/PH 构建标记，所以不把它们当成 S3000 的替换固件。哈希和限定条件见 [S3000 VZ 固件家族核验](vz-firmware-family-audit.md)，机器可读结果为 `reports/vz-firmware-family-validation.json`。

只读复核本机 PCI/sysfs 显示 S3000 暴露两个 64 KiB 非预取 BAR 和一个 16 GiB 64-bit 预取 BAR；当前没有绑定内核驱动，PCI `BusMaster` 仍关闭。设备仍列出 `mtgpu` 作为可匹配模块，但没有确认绑定。此 Guest 的 PCI 环境包含 QEMU/QXL/virtio 设备，且 `/sys/bus/vmbus` 不存在。这说明当前可见的是 QEMU PCI 端点，而没有可供直接复用的 VMBus 设备；它仍不能给出 PVZ map/unmap 的 Host ABI。这里只读了 PCI 资源，没有操作设备或驱动状态。

### Guest 固件选择调用链核验（2026-09-29）

对当前候选 `build/official-vgpu-2.3.0-guest-heapcount-fwbasepubfix-audit-20260929/objs/x86_64/mtgpu_core.o_binary` 单独反汇编后确认：Guest 模式 getter 直接读取 `mtgpu_load_windows_firmware`；PVR wrapper 调用该 getter；`RGXLoadAndGetFWData` 调用 wrapper，真分支传 `.vz.win`，假分支传 `.vz.linux`，之后两支都进入 `OSLoadFirmware`。模块 `.modinfo` 同时声明两个名字，staging 实际包含 `musa.fw.1.0.0.0.vz.win`，哈希 `bd9b569dc8bee47ffcb606081aaf8a1942456510797b6ccb95397f5da56fa17e`。由于源码默认值为真，当前离线候选默认请求的镜像已经有文件可供 `request_firmware` 查找；这排除了“默认误请求不存在的 `.vz.linux`”这一静态阻塞。机器可读检查在 [Guest 固件 selector 核验](../scripts/verify-guest-fw-selector.py)，该检查不触碰硬件。剩余问题仍是 Guest FW_MAIN / `FW_PREMAP7` 与 Host OSID 7 固件槽的实际 backing，以及固件能否启动；当前没有运行态证据。

### Linux v1 Guest 信息页快照解码（2026-09-29）

为避免继续用 Windows 探测页推断 Linux Host ABI，新增离线工具 `scripts/decode-linux-vgpu-info.py`。它只读取传入的普通文件；检查长度、magic、version 和最多 64 个段记录后，输出 OSID、flags、VM 内存大小、VGPU/VVPU 段、`fw_heap_base/size`、`mmu_heap_base/size`、扩展字段及输入 SHA-256。解码器按 Linux 2.3.0 `mtgpu_mdev.h` 中 `struct vgpu_info` 排列：段表起点 `0x28/0x438`，FW heap `0x848/0x850`，MMU heap `0x858/0x860`，结构长度 `0x8a0`。它明确保留基址的原始数值，不把 GPU/card PAddr 擅自转换成 BAR2 offset、Guest IPA 或 Host backing。

`python3 -m unittest discover -s tests -p 'test_*.py'` 的 58 项测试通过；解码器测试还用 x86-64 C 对齐模型复核字段偏移，并拒绝 version 2、越界段数、截断页和地址回绕。对现有采集物盘点后，`reports/device-info.bin` 及保存的 `info_raw` 信息页均为 Windows version 2、OSID 4；没有 Linux version 1、OSID 7 的原始页。因此解码器提供了正确的后续证据入口，但没有增加 OSID 7 基址或 BAR2 backing 的实测证据。示例：

```sh
python3 scripts/decode-linux-vgpu-info.py /path/to/linux-v1-vgpu-info.bin
```

本轮保持离线；未访问 BAR/MMIO，未安装、加载或卸载驱动，也未操作 PCI 状态。

### V2 地址转换边界回归（2026-09-29）

为锁定 Windows 参考转换器的适用范围，扩展 `tests/test_guest_vgpu_info_compat.py`：直接把 FW 段 GPU 地址作为输入时必须返回 0；合成的 `flags 0x8/0x10` 段不进入 BAR2 packed-map 游标，后续 `flags 0x2/0x4` 段仍从游标 0 开始映射。目标测试 12 项、全套 Python 测试 74 项均通过。扩展后的 oracle 验证重新用保存的 Windows V2 页和 `mtkm64.sys` 中 `FUN_140027ab4` 做对照：142 个 BAR2 边界/随机地址、5 个窗外地址及 5 个合成 flags 边界全部与参考函数一致，详情在 `reports/v2-gdpa-compat-oracle-validation.json`。

这加强了 V2 helper 的 fail-closed 边界，但没有证明 Linux 2.3 Guest 的七个静态调用点运行时都传入 BAR2 aperture 地址；超出该输入域会返回 0。只更新了测试、oracle 核验脚本与报告，没有重建或替换 audit8 模块；未访问设备或宿主。

### V2 Guest VPU 共享内存地址适配（2026-09-29）

Linux 2.3 Guest 的 `mtgpu_device_memory_fixup` 在初始化 `vgpu_share_mem` 时，把信息页 `+0x448` 原样写入 `vpu_share_mem_dev_addr`。该字段符合 Linux v1 布局；保存的 Windows v2 页在此偏移已不是 VPU 共享段。

Windows `mtkm64.sys` 的 `FUN_140026b30`（反编译文件 `decompiled/mtkm64.sys/decompiled.c`，约第 32407 行）提供了 v2 语义：信息页 flags 的 `0x80` 置位时，查找 `flags&0x20` 的段、要求至少 2 MiB，再把段基址作为 BAR2 偏移加到设备 BAR2 GPU 基址。保存的 V2/OSID 4 页有唯一共享段，偏移 `0x43000000`、大小 2 MiB。本机只读 PCI resource 表显示 BAR2 基址 `0x800000000`、窗口 16 GiB，该共享范围位于 BAR aperture 内。Linux `struct mtgpu_device` 中 `pcie_mem` 起始于 `+0x78`；同版核心 `mtgpu_device_common_init` 以 PCI BAR2 index 2 填充 `+0x78/+0x80`，因此新增 helper 使用实际 BAR2 基址和窗口长度。

后链接脚本把 `mtgpu_device_memory_fixup+0x390` 原先加载 `info+0x448` 并写入共享结构的 19 字节窗口，改为调用双版本 helper。V1 仍读 `+0x448`；V2 严格要求有效 magic、版本、header `0x80`、唯一 `flags&0x20` 记录、至少 2 MiB、段落落在 PCI BAR2 窗口内，并检查 BAR 窗口末端、段末端及最终 GPU 地址的 64 位溢出。异常 V2 页会把该字段置零；helper 返回原共享结构指针，使后续核心指令继续按原方式写入 32 KiB 大小。VPU 初始化以外的数据流未修改。

最终离线候选为 `build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-compat-sharedmem-audit10-20260929/mtgpu.ko`，SHA-256 `8af125431e66ade89554bb11f0e50a0d76f7b1499dafac59ec15565c01ae9ccb`，staging 模块哈希一致。全套 78 项单测通过，其中捕获页返回 `0x843000000`，并覆盖 V1、缺失/重复标记、短段、BAR 越界和 BAR 窗口末端溢出。Windows `FUN_140027ab4` GDPA oracle 的 142 个地址以及额外窗外/flags 边界仍全部一致；后链接审核确认 58 个预期字节变化、四个 ftrace/重定位表保持不变，V1 原函数体和 7 个调用点不变。S3000 PCI alias、`6.12.107+deb13-amd64` vermagic 与 Guest PCI 初始化顺序检查通过。构建有预编译核心既有的 objtool ENDBR 数据重定位警告，没有新编译错误。

这只是地址字段适配的离线候选，没有证明 Linux Guest 实际收到 Windows 同布局的 V2 页，也未证明 Host 建立了共享段 backing、OSID 7 heap/PVZ 映射有效或 VPU/GPU 固件能启动。候选未安装、未加载，未访问 BAR/MMIO，也未操作宿主机。

### V2 信息页缓冲区容量修正（2026-09-29）

继续沿完整字段访问链核对时发现容量不匹配：Linux 2.3 Guest 的 `mtgpu_device_memory_fixup` 按 v1 `struct vgpu_info` 大小 `0x8a0` 分配零初始化缓冲区；新增 V2 解析器会访问段数 `+0xc50`、段记录（最高至 `+0xc40`）和 PB 字段 `+0xc98`，原分配因此不足。Windows `mtkm64.sys` 的初始化路径分配 `0x1000`，刷新路径复制 `0xcc8` 字节，保存的 V2 页文件也是 4096 字节。先前解析测试使用 4 KiB 合成/采集页，覆盖了字段算法，却没有覆盖 Guest 核心实际分配尺寸。

修正后的后链接补丁在 `mtgpu_device_memory_fixup+0x24a` 将对齐分配表达式中的 `0x89f` 改为 `0xfff`，使同一零初始化缓冲区至少为 4 KiB；其前 `0x8a0` 字节和所有 V1 字段偏移保持不变。新测试构造满 129 条记录的 V2 页，并验证最后一条可同时解析 FW_MAIN 与 VPU 共享段。审计报告确认相对本次 Kbuild 基线只有 60 个受控代码字节变化，四个 ftrace/重定位表不变；79 项 Python 测试、142 个 Windows `FUN_140027ab4` 地址 oracle 对照以及额外窗外/flags 边界核验通过。

最新离线候选为 `build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-compat-sharedmem-audit11-20260929/mtgpu.ko`，SHA-256 `a4d5947856c14b84a2c4b1e524b885a1f791d09a7f8782bfefefe914c86adbf5`，staging 哈希相同。构建 vermagic 为 `6.12.107+deb13-amd64`；链接审计与 Guest PCI probe 顺序检查通过。该更改仅保证本地缓冲区容量足以容纳 Windows V2 布局，不证明当前 Host 会向 Linux Guest 发布 V2 页，也不证明该页的内存可被 Host 安全写入、OSID 7 backing/PVZ、固件或 GPU 执行正常。候选仍未安装或加载，未访问 BAR/MMIO，也未操作宿主机。

### V2 VPU 共享段长度适配（2026-09-29）

继续追 Windows V2 的共享段消费链发现，地址之外还有长度差异。`mtkm64.sys` 的 `FUN_140026b30` 查找 `flags&0x20` 段并拒绝小于 2 MiB 的记录，随后把映射长度设为 `0x200000`；WDDM 后续 `FUN_14000b5b4` 通过 `MmMapIoSpace` 按该长度映射并复制整个区域。查询接口 `FUN_140022ea8` 返回这一长度和共享段地址。相比之下，Linux 2.3 Guest 的 `mtgpu_device_memory_fixup` 固定向 `vgpu_share_mem.vpu_share_mem_size` 写 `0x8000`。

双版本 helper 现在一并发布地址和长度：V1 继续使用信息页 `+0x448`、32 KiB；有效 V2 仅在唯一共享段及 BAR2 窗口完整通过检查后发布 `BAR2_base + segment.base` 和 2 MiB；检测到 V2 但校验失败时发布零地址、零长度。后链接补丁移除了原先覆盖所有版本的 `0x8000` 写入，改由 helper 负责该字段，返回的共享结构指针和后续屏障保持不变。17 项针对性的 vGPU info helper 测试通过，包含 V1 长度、采集 V2 页 2 MiB 长度、满 129 条段记录及坏 V2 页零长度；全套 79 项 Python 测试通过。

audit12 离线候选为 `build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-compat-sharedmem-audit12-20260929/mtgpu.ko`，SHA-256 `7783296d62539a7a29444e743e7d036aefed7ad00003791e86369f382b97ff5e`，staging 哈希一致。Windows GDPA oracle 的 142 个样本和额外窗外/flags 边界仍通过；同次 Kbuild 基线对比显示 68 个受控代码字节变化，四个 ftrace/重定位表未变，Guest PCI probe 调用顺序检查通过。跨 Windows/Linux 的 VPU 共享长度对应关系目前由反编译静态路径支持，尚无当前 Host/OSID 7 的运行态确认。候选未安装或加载，未访问 BAR/MMIO，也未操作宿主机。

### V2 VPU ring 映射适配（2026-09-29）

继续追 Guest VPU 初始化时发现，前一版虽然把 Windows V2 的共享段地址和 2 MiB 长度写入 `vgpu_share_mem`，但预编译的 `vpu_init_guest_mem` 仍从 `mt_chip.bar_base` 映射 32 KiB。反汇编显示它把 `[chip+0x24f8]` 作为 `os_ioremap` 地址、固定传入 `0x8000`，再把映射指针存到 `[chip+0x1bd88]`。用同一 Kbuild 选项编译的 `offsetof` 探针确认 `mt_chip.bar_base==0x24f8`；Kbuild DWARF 布局进一步确认 `mt_chip.vm` 位于 `0x1bd68`、`mt_virm.mem` 位于该结构内 `+0x20`，所以 mapper 指针槽 `0x1bd88` 正是 `mt_chip.vm.mem`。补丁现在包含这两个布局关系的 `BUILD_BUG_ON`，如果后续内核配置或结构发生漂移会在编译期失败；因此 V2 ring 原来会落在 BAR2 起点，而不是 Host 与 Guest 已协商的共享段起点。

边界与用途也不同：Windows `mtkm64.sys/FUN_140026b30` 对 V2 `flags&0x20` 段要求至少 2 MiB，并映射完整 2 MiB；Linux Guest 的可见 ring 则是该区域内一个固定 32 KiB 子窗口。反汇编跟到的 `set_avail`/`wait_get_used` 记录以 `0xe8` 字节步长排列，ring 负载从 `0x120` 开始、每槽最多复制 `0xd8` 字节，使用状态在 `0x1f8`；索引最多 32 个，观察到的最高读写结束位置为 `0x1e14`，仍落在 32 KiB 内。因此保留 Host 发布 2 MiB 的长度，同时只让 Linux 内核 ring mapper 映射该 V2 段开头 32 KiB。

新增默认关闭的 `MTGPU_GUEST_PATCH_VPU_SHARE_MAP=1` 构建开关及补丁 `patches/mtgpu-v2-guest-vpu-share-map.patch`。Guest VPU 先验证信息页 magic，并只接受 V1/V2；损坏或未知页版本直接走错误清理，避免把坏 V2 页落入旧 BAR2 起点映射。对有效 V2 页，它读取 `mtdev->vgpu_shm` 中已由双版本 helper 解析出的共享设备地址，helper 再检查 magic/version、header `0x80`、2 MiB 长度及 BAR2 窗口边界。Guest VPU 初始化前只临时把 `chip->bar_base` 设成该地址，调用预编译 ring mapper 后立即恢复原 BAR2 基址，避免影响后续 VPU 视频内存地址转换。V1 保持原有映射；无法解析 V2 地址或 32 KiB 映射失败时，VPU component 初始化走现有错误清理路径。组件顺序表中 PVR component 排在 VPU component 之前，静态上先由 PVR 初始化并填充信息页/共享结构，再运行 VPU bind。

新增 helper 单测覆盖采集 V2 页映射到 `0x843000000`、V1 仍走旧路径、共享标记缺失、BAR2 越界及错误长度；链接后反汇编另确认 VPU bind 会拒绝损坏 magic 和未知版本。最新 audit13-r7 离线构建位于 `build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-sharedmem-vpumap-audit13-r7-20260929/mtgpu.ko`，SHA-256 为 `94ebca9846966d9c8b5825b66eace485e984094ab542249ddb4fb28132b78348`，staging 哈希一致，vermagic 为 `6.12.107+deb13-amd64 SMP preempt mod_unload modversions`。82 项 Python 测试、Windows GDPA oracle 142 个样本、Guest PCI probe 顺序检查及 ftrace 元数据完整性检查通过。结构、ring 访问范围和 VPU mapper 均为静态核验，Windows 捕获页是 OSID 4，不能证明 Linux OSID 7 实际收到 V2 页或共享段 backing 已建立。OSID 7 heap/PVZ、固件和 GPU 执行仍未验证；该模块只构建到 staging，未安装/加载，也未访问 BAR/MMIO。

audit13-r7 可离线复建：

```sh
MTGPU_GUEST_BUILD_DIR="$PWD/build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-sharedmem-vpumap-audit13-r7-20260929" \
MTGPU_GUEST_PATCH_PHYSHEAP_COUNT=1 \
MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT=1 \
MTGPU_GUEST_PATCH_VGPU_ADDR_COMPAT=1 \
MTGPU_GUEST_PATCH_VPU_SHARE_MAP=1 \
scripts/build-official-vgpu-guest.sh
```

可复核的布局、地址、ring 范围和候选散列记录在 `reports/vpu-v2-shared-ring-audit.json`。

### 候选与内核活动模块身份复核（2026-09-29）

扩展只读诊断脚本读取 `/sys/module/mtgpu/notes/.note.gnu.build-id`，并支持将指定离线 `.ko` 的 build-id/SHA-256 与活动模块对照。该只读快照中活动模块状态为 `coming`、引用数 1、taint `OE`，build-id `3f50769528dc4c23b4ed36dda8cf89d7a9339fdc`；它与已标记为 discarded 的 `build/discarded-ftrace-oops-diagnostic-experiment/mtgpu.ko` 一致，与最新 audit13-r8 候选 build-id `671caf9a6d0512e6b88c49a1686099aa3acd1368` 不同。S3000 `0000:00:0e.0` 没有驱动绑定，唯一 DRM 节点是 QXL `card0`。该快照当时确认 audit13-r8 是离线候选，不能把旧模块状态当作 r8 的运行结果。更新后的 [只读身份快照](mtgpu-module-identity-readonly.json) 也保留了上一版 r7 的离线身份。

快照位于 [模块身份只读核验](mtgpu-module-identity-readonly.json)，诊断实现和用法见 `scripts/guest-doctor.py`。采集只读取 sysfs 和模块 ELF note/hash，没有读取 PCI config/MMIO，也没有加载、卸载或恢复模块。

### VPU ring 映射阶段复核共享页一致性（2026-09-29）

再核对 VPU bind 到预编译 `vpu_init_guest_mem` 的数据链后，补强了映射阶段的 helper：它不再只凭 `vgpu_share_mem` 中“落在 PCI BAR2 aperture 内”的地址决定映射，而是重新读取当前 V2 信息页，检查段数不超过页面容量、恰好一个 `flags&0x20` 共享段、段大小至少 2 MiB、完整段仍在 BAR2 aperture 内，并要求共享结构发布地址等于 `BAR2_base + segment.offset`、发布长度恰为 2 MiB。这样信息页在初次解析后变化、共享结构被改写到另一个有效 BAR2 地址、或长度不符时都会拒绝映射。

扩展后的定向 helper 测试 20 项通过，新增覆盖了“另一个仍在 BAR2 内的假地址”、超长发布长度，以及初次发布后信息页共享段偏移改变等情况；全套 Python 测试 85 项通过。链接后反汇编也确认映射 helper 重新读取 `+0xc50` 段数并扫描 `flags&0x20`，而不是只在源码中包含这段验证。Windows `FUN_140027ab4` 转换器 oracle 的 142 个地址样本仍全部匹配。新离线候选为 `build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-sharedmem-vpumap-audit13-r8-20260929/mtgpu.ko`，SHA-256 `2333194a473506d9390f5d7444099d083c22ce6c777f891b0d4c917b121899c3`，ELF build-id `671caf9a6d0512e6b88c49a1686099aa3acd1368`，vermagic `6.12.107+deb13-amd64 SMP preempt mod_unload modversions`；staging 哈希一致。Guest PCI 初始化顺序、7 个原 V1 地址转换调用点、68 个受控后链接改动和 ftrace 元数据完整性检查通过。构建中可见随包 DRM/PVR 旧源码已有的内核警告；没有新的 C 编译错误。

该核验只提升地址/长度数据的一致性检查，不建立 Host backing，也不证明 OSID 7 信息页、PVZ 映射、固件启动或 GPU 执行。候选只在本机离线构建，未安装或加载，也未读取 BAR/MMIO 或操作宿主设备。

### 本机 Guest heap-count 失败路径与候选二进制交叉核验（2026-09-29）

把此前本机日志中的 `Driver mode: Guest`、`mem_mode=1`、`PhysHeapsInit: Failed to init heaps [644]` 与 `SysDevInit` 反汇编对应后，确认 `[644]` (`0x284`) 来自 heap descriptor 实际数与期望数不一致的错误分支：核心在 `SysDevInit+0x434` 比较两者，不等时跳到 `+0x78f` 并设置错误码 `0x284`。Local Guest 的构造路径产生 4 个 descriptor，原始计数却期望 5 个。

新增 `scripts/verify-guest-physheap-count-candidate.py`，并接入构建脚本。它检查链接候选中的 Guest-only trampoline、原始非 Guest `add $6`、descriptor 比较、错误分支和 `0x284` 错误码。对 audit13-r8 的检查确认：该 Guest 分支把本机 `mem_mode=1` 对应的期望值从 5 调到 4，和静态生成数相符；Host/Native 共用计数仍为原值。机器可读结果位于 `build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-sharedmem-vpumap-audit13-r8-20260929/guest-physheap-count-candidate-validation.json`。

这证明候选二进制针对已观察到的 `[644]` 失败点包含了对应修正，不证明它已在运行态通过该检查；当前内存里的旧模块仍处于 `coming`，audit13-r8 仍未加载。Windows `mtkm64.sys`、`mtdispkm64.sys` 和 `mtvpukm64.sys` 反编译文件中没有找到 Linux PVR 固定 `E1C` premap 地址，因此现有 Windows 参考不能提供该地址到 Host OSID 槽的直接映射公式；没有据此伪造地址转换。

### audit13-r10 最终候选完整重建与 heap-count 校验收尾（2026-09-29）

为确保 heap-count 校验报告描述最终二进制，而不是随后还会被 vGPU 信息页补丁改写的中间 .ko，将校验调用移动到所有后链接补丁之后，并在全新目录完成完整 Kbuild。最终候选为 build/official-vgpu-2.3.0-guest-vgpu-info-v2-addr-sharedmem-vpumap-audit13-r10-20260929/mtgpu.ko，SHA-256 5c2b68a02f7a772ccacb729d6e99a2b872e1606863bf93d6995768cd6ef53d92，build-id 59b8554cbed7738e56ea7552fd0ac7d847901589，vermagic 6.12.107+deb13-amd64 SMP preempt mod_unload modversions。候选与 staging 副本 SHA-256 完全一致；heap-count 报告现与最终 .ko 的 SHA-256 一致，并确认 Guest-only 计数 trampoline、非 Guest 原始计数指令、[644] / 0x284 错误路径均在最终链接文件中。

85 项 Python 单测、V1/V2 地址兼容检查、Guest PCI 初始化顺序检查及 ftrace 元数据完整性检查通过。VPU ring 布局和 Windows GDPA oracle 的既有定向验证仍记录在上述机器审计中。此候选仍是离线构建，未安装、未加载、未触碰 BAR/MMIO；Guest 设备初始化、OSID 7 信息页与 heap backing、PVZ、固件运行和 GPU 执行均未得到运行态验证。稳定路径的 heap-count 校验结果见 reports/guest-physheap-count-candidate-validation.json。

### Guest 重启后的首次固件连接确认（2026-09-29）

重启后原版 `mtgpu` 仍在 `[644]` heap-count 检查失败。来宾 BAR0 只读状态为
Guest=0/FW=1；正常解绑原驱动后，`fresh-trial.py` 完整预检通过。一次限定试验使用
`mt_guest_probe.ko`（SHA-256 `04bf739dc21a0cd13a1f0620929cda5a6ddc5436a929a4d39f9e42de9ec5c3df`），
共享通道注册和 RPC 成功，信息页为 V2/OSID 6，显存保留及启动资源构造成功。
固件 `connect_result=0` 表示试验轮询中观察到 FW=2 且 started 非零，这是本机首次
连接确认。

同一试验随后请求断开但返回 `-108`，当前读数回到 Guest=2/FW=1/started=0；模块
`pinned=1`，因此保留固件和启动资源，不尝试解绑、卸载或回写显存。当前仍只有 QXL
`card0`、`render_ready=0`，无 MTT DRM 节点，不能称为 GPU 加速可用。快照位于
[`fresh-trial-live-validation.md`](fresh-trial-live-validation.md) 及其指向的
`build/fresh-trials/20260929T144220Z-eb28ef82/`。

`fresh-trial.py` 现在支持 `--runtime-context`，下一次干净 Guest 启动可在同一预检后
测试连接成功时发布运行上下文。当前活动的 pinned 会话会被预检拒绝；未建立上下文
撤销协议前不得对它执行解绑/卸载。即使运行上下文发布成功，也仍需实现和验证 DRM/GEM
用户接口与实际 GPU 工作。


### 同一启动中的 V2 协商与实际地址转换修正（2026-09-29）

本轮 r16b/r17/r18b 真机证据已取代此前对 Linux OSID 7 信息页和 dispatcher 字段的推断。当前 OSID 为 6；缺失请求能力位会收到与 Linux 2.3 头文件不兼容的 typed V1 布局。补齐 V2 协商、纠正 platform 字段和 PVR 相对 PA 语义，并将遗漏的 7 个核心调用点接入 dispatcher。当前 V2 页及真实配置链上的转换已核验；PVR heap/MMU 布局仍未适配完成。r15b BAR2 起点 fallback 已撤回。详见 [本轮完整实机报告](guest-info-negotiation-live.md)。


## 2026-09-29 r19b–r22b 真机内存与 MMU 验证

当前 V2 / OSID 6 的私有池、通用池和 FW 池布局已接入 PVR。LMA 两页读写恢复、MMU 根上下文、FW 内核虚拟堆、GPU-local PMR 两页映射/撤销均通过真机验证。最终 PC/PD/PTE Host 地址正确，撤销后两页均失效，模块正常卸载。96 项 Python 检查通过。固件区与 PB 区未变化；未执行固件加载或 GPU 命令，没有 MTT render 节点。详情与失败修正记录见 [真机 MMU 进展](guest-mmu-live.md)。


## 2026-09-30：r23 固件连接与真实任务分界

Guest 实验 transport 已成功连接固件并完成 DM1 NULL 命令 fence；首个 TQX 256 字节复制返回 0x101 故障，当前会话及 pending VM/BO 全部保留。实际上传命令流/DMA 与 Windows 原始编码结果一致，目标仍是初始值，尚无硬件加速。此阶段运行的是 mt_guest_probe，不能视为官方 mtgpu/PVR 栈初始化完成。详见 [完整实测与模块身份](r23-runtime-marker-tqx.md)。


## 2026-09-30：r24 TQX 页表权限修正

r23 真机普通 BO 使用 flags=3，Windows 映射器生成 PTE bit1=只读。原厂 Linux RGXDerivePTEProt8 提供独立语义交叉验证。候选改用 flags=0，并在 TQX 上传前检查目标、DMA 和引擎状态映射可写。所有离线/内核 RAM 检查通过，尚未经过 Guest 重启后的真机复制验证。详见 [权限差异、候选身份和下一轮条件](r24-tqx-map-permissions.md)。

## 2026-09-30 r25：重启后的保留故障

Guest 重启已确认，当前 Guest0/FW0；完整 8 MiB 固件与 r23 故障快照相同。旧自动加载 mtgpu 仍 [644] 失败，已正常卸载。新增严格限定的离线信息查询/只读固件采集 helper 和离线快照比较工具，W=1 和实际采集/卸载通过。原始 Windows 0x101 handler 的 121 项案例通过，确认它生成 WDDM type 9 故障通知，但固定字段不能定位实际故障地址。r24 候选未加载、未重试，不修改状态绕过预检，也未执行 MPC HWR。详见 [r25 记录](r25-after-reboot.md)。

## 2026-09-30 r28：实际 TQX 复制成功

关机重启带来 OSID 4 新会话。精确核验已退役 DISCONNECT 并完成 Guest OFF 收尾后，r24 实验驱动成功连接、完成 marker，并实际复制 256 字节。源/目标/保护区与实际可写 PTE 通过读回核验。当前保留 ACTIVE 会话，尚未开放 DRM/render 或完成官方 PVR 用户态适配。见 [r28 完整记录](r28-hardware-copy-success.md)。
