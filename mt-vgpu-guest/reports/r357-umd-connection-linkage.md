# r357：UMD 真实建连链路 recon——`GetSrvHandle` 返回有效指针，`PVRSRVBridgeCall` 到达桥侧入口（离线 fabricated，零硬件触碰）

## 结论（实测）

1. **链路语义（语料实测，UMD SHA `b3058c02…34237b0` 对版）**：
   - `GetSrvHandle @ 0x3c1c0`（文件偏移；Ghidra 镜像基址 `0x100000` 下记为 `0x13c1c0`——r354 与 r355 的两种写法实为同一函数，特此澄清）：`return param_1 ? *param_1 : 0`，即读连接结构体首 qword。
   - 连接结构体 0xd0 字节（`FUN_0013b7c0` 内 `PVRSRVCallocUserModeMem(0xd0)`）；首 qword 由 `OpenServicesDevice`（`FUN_00192550`）写入：`*param_4 = piVar4`，其中 `piVar4` 为 0x10 "user services handle" 结构，其首 dword 为 DRM fd（`*piVar4 = local_3c`）。另：`+0x8=1`（`*(undefined4*)(__ptr+1)=1`）、`+0xc`/`+0x18` 由同一调用回填、`+0x14` 为 flags（`uVar6`）。
   - `PVRSRVBridgeCall`（`FUN_00192930`）：`ioctl(*param_1, 0xc0206440, …)`——解引用取 fd 后发桥 ioctl；errno 路径：`EAGAIN` 重试，`ENOTTY` → "Call to bridge module … not enabled in the Server" → 返回 `0x26`。
2. **fabricated 验证（ctypes 直调真实 UMD `.so`，无 GDB、无模块、无 DRM 设备）**：7/7 通过——
   - 地址约定交叉核对：`dlsym(GetSrvHandle) == load_bias + 0x3c1c0`（`GetSrvHandle` 在 dynsym 中为 `T` 导出符号，可直接 dlsym；`PVRSRVBridgeCall` 为内部符号，走基址+偏移）；
   - `GetSrvHandle(conn)` 返回句柄指针（`0x7f…` 形态，非小整数）；`GetSrvHandle(NULL) == 0`；
   - r354 artifact 复现（读层面）：首 qword 置 `0x6000` 时返回 `0x6000`（仅读、不解引用，故不崩）；
   - `PVRSRVBridgeCall(handle, 0x82, 0xc, …)`：`ioctl(/dev/null, 0xc0206440)` → `ENOTTY` → 调试路径 → 返回 `0x26`，**无崩溃**（对比 r354 同一调用在 `0x929ce` 以 `rax=0x6000` SIGSEGV）。
3. **设备打开路径（盘点，供 r358）**：`FUN_001a3ab0`（`_GetFd`）→ `FUN_001a4af0(0x80)` / `FUN_001a4a70(0)` → `FUN_001a4940` 扫描 render minor `0x80–0xbf`，逐个 open 后按 driver 名匹配 `"pvr"`（首轮）/`"mtgpu"`（次轮）；随后 `ioctl(fd, 0x40046445)`（SRVKM_INIT）+ `BridgeConnect`（`FUN_001386c0`）。`PVRSRVConnectionCreateDevice`（`0x48f80`）之后还做 `GetFeatures`、`*(features+0x54)==1` 对齐门、`BridgeAlignmentCheck`（`0x27`）。

## fabricated / 真机分界（盘点）

- fabricated 已验证到：指针链路语义、`GetSrvHandle` 读语义（含 NULL/artifact）、ioctl 分发到达与 errno 处理路径。
- 必须真机（r358）：open 真实 `renderD128` 得到真实 fd、`SRVKM_INIT` 成功、`BridgeConnect` 建连、`GetFeatures` 返回真实特性页、`*(+0x54)==1`、`BridgeAlignmentCheck` 通过。fabricated 的 `/dev/null` fd 在 `SRVKM_INIT` 即止（`ENOTTY`），其后一律不可验证。

## r358 活体前置输入与验收判据（需批准）

- 前置：freeze 会话完好（`mt_guest_probe` 绑定、`mt_pvr_bridge` 在载）；`/dev/dri/renderD128` 存在；UMD `.so` SHA 对版；L3 基线已绿。
- 步骤（只建连、不提交 GPU 工作）：经 `PVRSRVConnectionCreateDevice` 真实路径（或等价最小 harness）open `renderD128` → 建连 → `GetSrvHandle` → 一次 `PVRSRVBridgeCall`（SRVCORE connect）。
- 验收：`GetSrvHandle` 返回值高位置位（指针形态，非 `0x6000` 类小整数）；桥侧 `pvr_cmd_connect` 收到首包（dmesg/bridge 日志可见）；`PVRSRVConnectionCreateDevice` 返回 0。
- 红线：不跑 TA/3D 提交；窗口结束即恢复 freeze（默认桥 + L3 复绿，双绿才关账）。

## 实测与边界

1. 全程离线 fabricated：ctypes 直调 `.so`（默认 RTLD_LOCAL），除 `/dev/null` 外未 open 任何设备；无模块、无 PCI、无 GPU。会话 freeze 继续。
2. 证据（0600）：`r357-linkage-fabricated.txt`（7/7 输出全文）。
3. 推断标注：0xd0 结构 `+0x8/+0xc/+0x14/+0x18` 的字段语义来自单点反汇编，未在活体交叉验证——r358 建连成功后可顺带确认，不阻塞。
4. 地址写法说明：本报告统一用文件偏移（`nm`/`objdump` 口径）；Ghidra 口径 = 文件偏移 + `0x100000`。

## 门禁

- 新增 `tests/test_umd_connection_layout.py`（6 项：5 函数地址钉、0xd0 分配点、`GetSrvHandle` 读语义、`0xc0206440` 常量、`OpenServicesDevice` 写链）；反向验证（`0x3C1C0`→`0xDEAD00` 注入即红）通过后还原。
- `make -C mt-vgpu-guest check-offline`：400 Python（含新增 6）+ 299 C 全绿。
