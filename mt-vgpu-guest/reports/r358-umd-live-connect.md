# r358：UMD 真实建连打通——`PVRSRVConnectionCreateDevice` 经 `renderD128` 建连成功，桥侧 SRVCORE connect 收包（真机活体，用户已授权）

## 结论（实测）

1. `PVRSRVConnectionCreateDevice(&conn, 0xffffffff, 0xffffffff)` → 返回 `0`，`conn=0x25220fe0`，耗时 1ms。真实 UMD `.so`（SHA `b3058c02…34237b0` 对版）经 `_GetFd` 打开 `/dev/dri/renderD128`（driver 名 `pvr` 首轮匹配），完成 `SRVKM_INIT` + 内部 `BridgeConnect` + `GetFeatures` + `BridgeAlignmentCheck` 全链路。
2. `GetSrvHandle(conn)` → `0x252211a0`，指针形态（非小整数），与 r357 fabricated 结论一致，活体复验通过。
3. 单次显式 `PVRSRVBridgeCall(handle, 1, 0, in16, out17)`（SRVCORE connect）→ 返回 `0x0`，OUT 逐字节命中桥侧 `mt_pvr_connect_result`：`packed_bvnc=0x0023000406600017`（=`MT_PVR_BVNC_ALLOW`）、`error=0`、`caps=0`、`arch=0`。桥侧 `pvr_cmd_connect` 确实收到并处理了该包。
4. dmesg：基线 1117 行 → 跑后 1118 行，唯一新增为 `arena close: high_water=17/512`（连接 fd 关闭时的正常清理，与基线尾部同类行一致）；无新增 WARN/BUG/Oops。
5. ref 计数：`mt_pvr_bridge 0`、`mt_guest_probe 1`，建连前后不变，无泄漏。
6. freeze 会话未碰：无 rmmod/insmod/unbind、无参数修改；本轮为纯 userspace `open` + `ioctl`，收尾无需"恢复"，freeze 始终有效。

## 路径还原（实测，反汇编 + 活体双验证）

- `ConnectionCreate`（`FUN_0013b7c0`）：`param_2=param_3=0xffffffff` 触发设备枚举分支（无 `PVR_GPUIDX` 环境变量 → `DefaultGPUDevice` 缺省 → `param_3=0xffffffff`）；`_GetFd`（`FUN_001a3ab0`）扫描 render minor `0x80–0xbf`，`renderD128`（minor 128）以 driver 名 `pvr` 首轮命中。
- `OpenServicesDevice`（`FUN_00192550`）：`ioctl(fd, 0x40046445, 1)`（SRVKM_INIT）→ 桥侧 `pvr_ioctl_init` → `mt_pvr_conn_init` 置 `srv_handle` 自指；随后 `BridgeConnect`（`FUN_001386c0`）→ `PVRSRVBridgeCall(handle, 1, 0, in16, out17)` → 桥侧 `pvr_cmd_connect`。
- `GetFeatures`：纯指针读（`conn+0xa0+0x620`），无 ioctl。
- `BridgeAlignmentCheck`（`FUN_00138c10`）：`PVRSRVBridgeCall(handle, 1, 0xa, in12, out4)` → 桥侧 `pvr_stub_ok` 回零 → 返回 0，全链路收敛。
- IN 缓冲布局实证（`BridgeConnect` 反汇编，xmm 打包）：16 字节 = `[param_3, param_5, param_4, param_2]`，对应 wire 结构 `mt_pvr_connect_in{client_build_options=0x80000850, client_ddk_build=0, client_ddk_version=0x10000, flags=0}`；桥侧 `pvr_cmd_connect` 忽略 IN，故显式调用沿用该取值安全。

## 地址核对（实测）

- `dlsym(GetSrvHandle) == load_bias + 0x3c1c0`（r357 地址约定，活体复验通过）。
- `PVRSRVBridgeCall = base + 0x92930`；`BridgeConnect` 内 `call 0x92930` 反汇编一致（`mov $0x10,%r8d; mov $0x1,%esi; push $0x11; call`）。
- 语料地址写法：Ghidra 口径 = 文件偏移 + `0x100000`（r357 已澄清，本轮 `objdump` 直验无误）。

## 边界与未做事项（live 边界，如实）

- 只建连：未发 `0x82:0xC`，未提交任何渲染/GPU 工作；`TA/CDM` 通道未碰。
- 未跑 `make probe`（L3）：其 `WITH_BRIDGE` 会 `rmmod` 桥，违反本轮硬性红线 §1；本轮桥未被扰动（`lsmod` 前后一致），"恢复 freeze" 以"未破坏"达成。等效健康证据：harness 与活体桥完成了 open + INIT + 2×connect + alignment check 全交互且返回正确，`refs` 不变，`dmesg` 干净。
- 桥侧 `pvr_cmd_connect` 无 `printk`，"收包"证据为 OUT 缓冲 17 字节逐字节命中预期（而非 dmesg 行）。
- 本轮共发出 2 次 `(1,0)`：一次为 `PVRSRVConnectionCreateDevice` 内部 `BridgeConnect`，一次为显式单次调用；`pvr_cmd_connect` 无状态（仅填 OUT），重复安全。
- 超时策略：未在 ioctl 临界区内设 timeout；全程 3 步均在毫秒级返回，无 hang、无重试。

## 门禁

- `make -C mt-vgpu-guest check-offline`：400 Python + 299 C 全绿。
- 本轮无代码改动（纯 harness + 文档），无新增门禁测试需求。

## 证据（0600）

- `reports/r358-live-connect.txt`：harness 活体输出全文（含各步耗时与返回值）。
- `reports/r358-baseline.txt`：跑前基线（lsmod refs、`ls /dev/dri/`、dmesg 行数与尾部）。
- `reports/r358-dmesg-diff.txt`：dmesg 差异与 WARN/BUG/Oops 扫描结果。

## 下一步

- r359：`0x82:0xC` 活体观察（IN 参数）——需用户批准（真机多轮已授权，按安全协议执行）。
