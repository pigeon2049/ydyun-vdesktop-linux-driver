# Legacy UM 与 Guest KMD 的 PVR Connect ABI 核验

日期：2026-09-29。检查对象是官方 Linux `mthreads-legacy-umd` 5.2.0 包及本地 S2000 vGPU 2.3.0 Guest-only KMD 源码。只做 ELF 静态检查和隔离解包，没有加载模块、打开显卡、安装系统包或执行 MUSA 测试程序。

## 结果

5.2.0 主 UMD 使用的 `libdrm_mtgpu.so` 需要 `MTGPU_IOCTL_VERSION=22`，与 2.3.0 Guest KMD 的版本 2 UAPI 不同。但同一 5.2.0 软件源还发布了独立的 `mthreads-legacy-umd` 包；其 `libsrv_um_MUSA.so.1.0.0` 仍使用 PVR Services DRM bridge。反汇编确认：

- 初始化调用 `0x40046445`，对应 KMD 的 `DRM_IOCTL_PVR_SRVKM_INIT`（DRM command `0x45`），初始化参数为 Services `1`。
- Services bridge 调用 `0xc0206440`，对应 `DRM_IOCTL_PVR_SRVKM_CMD`（DRM command `0x40`，32 字节 ioctl 包）。
- Connect 使用 bridge ID `1`、function ID `0`，输入 16 字节、输出 17 字节；这些大小和 ID 与 KMD 的 `PVRSRV_BRIDGE_SRVCORE_CONNECT` 及其输入/输出结构一致。
- Connect 输入携带 DDK version `0x10000`；KMD `pvrversion.h` 声明主/次版本 1.0，也编码为 `0x10000`。

这些证据说明 legacy UM 的服务初始化和 Connect ABI 与 2.3 Guest KMD PVR 1.0 接口一致，值得继续验证。该 legacy 包版本号是 5.2.0；Connect 匹配不能证明它所有 PVR bridge、固件接口、特性协商和显卡渲染路径均兼容，也不能证明它面向虚拟 Guest 或 S3000。

## 隔离暂存

包哈希和 ELF 哈希保存在 [机器可读检查结果](legacy-umd-pvr-connect-abi.json)。可运行：

```sh
python3 scripts/verify-legacy-umd-pvr-connect.py
```

包已仅解包到 `build/legacy-umd-pvr-connect-candidate/rootfs/`，未放入系统。静态依赖检查发现 `musa_dri.so` 需要 `libglapi.so.0`；当前系统解析不到该库。`libsrv_um_MUSA.so.1.0.0` 的系统库依赖可解析。暂存仅供后续 ABI 检查，不是已验证可用的用户态驱动。

## 后续核验

完整调用集合的核对已完成，结果见 [PVR legacy UMD bridge ABI 审计](legacy-umd-pvr-bridge-abi.md)。Connect 入口一致，但命令覆盖和若干输入/输出结构存在差异；不得将 Connect 匹配视为可直接配套。设备能力、芯片系列选择、固件版本协商、OSID 7 backing、Guest 固件启动和 GPU 执行仍未验证。
