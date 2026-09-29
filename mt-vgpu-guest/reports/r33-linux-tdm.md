# r33：Linux 原厂 UMD 提交格式与 Guest 描述符转换

2026-09-30。Linux 5.2.0 legacy UMD 的 QY1 TDM 寄存器尾部与当前 Windows vGPU
路径相容，完整 DMA 包布局不同。本轮实现并离线验证了严格的单任务转换器，尚未
接入设备 ioctl，也没有让原厂 OpenGL/Vulkan 库直接运行起来。

## 原始指令证据

继续使用已有完整 Ghidra 反编译库，不重新反编译。新增 `elf_reference_oracle.py`
在 Unicorn RAM 中装载固定散列 ELF 的 PT_LOAD 段和必要重定位，只允许运行列出的
函数与明确的分配/内存/查询模型。没有调用 ELF 构造函数、动态加载器、syscall 或设备。
一个 TLS 重定位仅记录、未模拟；涉及 TLS 的路径不在本次执行范围内。

Linux `libsrv_um_MUSA.so.1.0.0` SHA-256：
`b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`。
Windows `mtkm64.sys` SHA-256：
`0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33`。
Linux 函数地址按固定反编译库表示，为 ELF RVA 加 `0x100000`。

`RGXPopulateFeatureConfig` (`00151b20`) 查询 DRM version 后，只有 major=2 才设置
`features+0x54=2`，其它测试主版本 0/1/3 均为 1。原始 BVNC 表中的 19 组值、4 个
主版本共 76 例通过。这些 BVNC 是有效表项夹具，不代表本机实测硬件配置。
`RGXKickGfx` 与 `RGXTDMQueueTransferNew` 按此字段选择旧 PVR 与 DDK2 提交路径。
所以仅把设备名改成 mtgpu，或只实现一种 bridge，不能使这套 UMD 自动兼容。

## 布局差异与转换边界

原始 `SubmissionHeadCreate / RegionDescCreate / RegionCreate / KickCreate /
AddCswBuf / CmdGenerate` 生成一个 QY1 TDM 包，大小 `0x11b8`：

- Linux 包头 `0x58`，随后是 `0x1020` 字节 CSW；region 表在 `0x1078`。
- 单个 region 描述大小 `0x100` 不含寄存器尾部；kick 大小 `0x108` 包含尾部。
- Linux kick 固定部分 `0xe0`，40 字节寄存器位于整包 `0x1190`。
- Windows 包头为 `0x48`，kick 固定部分为 `0xc0`，相同寄存器位于 `0x160`；
  长度数组在 `0x200`，按核数保留 128 字节槽。

Linux 原始 `0015fba0` 生成命令 VA、长度数组 VA、engine-state VA、对齐后的命令
长度、PDS code、PDS initial 和 flags，40 字节全部与 Windows 寄存器格式对应。
长度数组需重定位到新的 Guest DMA 内，不能沿用 Linux CCB 内的地址。

`kernel/mt_linux_tdm.h` 只接受已验证 fresh record 对应的全零初始化、单 QY1 TDM
任务。它逐字节检查包头、CSW、region、kick 与寄存器，再调用已验证的 Guest 编码器
重建描述符和长度数组。任何未支持的任务标识、同步字段、非零 CSW 和扩展 region
都会拒绝。调用者仍须验证 BO 所有权、映射和实际命令内容；本转换器不提供这些权限。

80 例（40 填充、40 复制；1–8 核）转换后的整个 Guest DMA 和 submission view
与 Windows 原始指令逐字节一致。4536 个单字节变异加长度/地址/容量边界，合计
4544 例被拒绝，失败时输出保持原内容。

GFX/compute 的其它 region 仅用不透明标记验证了序列化位置，未生成有效图形寄存器，
没有提交硬件。GFX region 5 的 `0x33b0` 固定 kick 与复杂状态尚需进一步还原。

## 复现

```sh
python3 scripts/verify-linux-tdm.py
```

依赖系统 Python 的 unicorn 与 pyelftools（本轮已安装 python3-pyelftools）。
完整结果：`reports/r33-linux-tdm-validation.json`；原始 Linux/Guest 包、命令样本和
其它 region 的布局样本：`build/r33-linux-tdm/`。

本轮该项验证没有执行 GPU 任务、替换固件或操作宿主。它建立了用户态提交格式的
受限转换基础；PVR bridge、GFX 状态、标准用户态接口及完整图形加速仍未实现。
