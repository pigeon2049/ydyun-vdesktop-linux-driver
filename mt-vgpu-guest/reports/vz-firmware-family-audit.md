# S3000 VZ 固件家族核验（2026-09-29）

离线对照官方 Linux Guest 包的 PCI 表、`MODULE_FIRMWARE` 声明和随包 VZ Windows 固件内容。脚本 `scripts/verify-vz-firmware-family.py` 验证 S3000 `1ed5:0222` 在 3D/VGA 两张 PCI 表中都使用 `quyuan1_drvdata`，并与 `QUYUAN1` 系列 ID `1ed5:0200` 区分；源码只声明 VZ `1.0.0.0` 固件，且默认选择 Windows 固件。

随包 Windows VZ 固件的内容标记为：

| 文件 | 大小 | SHA-256 | 内嵌构建标记 |
| --- | ---: | --- | --- |
| `musa.fw.1.0.0.0.vz.win` | 41,632 | `bd9b569dc8bee47ffcb606081aaf8a1942456510797b6ccb95397f5da56fa17e` | `rgx_firmware_vgpu` |
| `musa.fw.1.1.0.0.vz.win` | 46,272 | `8ac6886d1a1e5856d270324f394186487293fcde874955cf30ef79d3f280639c` | `rgx_firmware_qy2_vgpu` |
| `musa.fw.1.2.0.0.vz.win` | 46,304 | `b8969763bce28ea990a3ff38413176326f4d02d04fa03011d96533ce0d215497` | `rgx_firmware_ph_vgpu` |

因此当前没有理由把 S3000 路径切换到 1.1 或 1.2：它们的标记表明是不同构建家族；本地源码也没有证明这两个镜像兼容目标虚拟设备。标记只能识别固件家族，不等同于完整兼容 ABI 验证。脚本只读源码和文件、输出 `reports/vz-firmware-family-validation.json`，没有上传或选择固件做硬件试验。

复核命令：

```sh
python3 scripts/verify-vz-firmware-family.py
```
