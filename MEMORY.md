# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](memory/MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](memory/MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](memory/MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](memory/MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](memory/MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](memory/MEMORY-HISTORY-2026-10-07.md)。
> 2026-10-08 起归档于 [`MEMORY-HISTORY-2026-10-08.md`](memory/MEMORY-HISTORY-2026-10-08.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。




## 本轮进展（r358：UMD 真实建连打通，真机活体）

- 建连（真机活体，纯 userspace，freeze 未碰）：`PVRSRVConnectionCreateDevice(&conn,0xffffffff,0xffffffff)`→0（`conn=0x25220fe0`，1ms），经 `_GetFd` 打开 `renderD128`（driver `pvr` 首轮匹配）→`ioctl(0x40046445)`→内部 `BridgeConnect`→`GetFeatures`（纯读）→`BridgeAlignmentCheck(1,0xa)`（桥 `pvr_stub_ok` 回零）。
- `GetSrvHandle(conn)`→`0x252211a0` 指针形态；单次显式 `PVRSRVBridgeCall(1,0,in16,out17)`→0，OUT 逐字节命中桥侧 `mt_pvr_connect_result`（`bvnc=0x0023000406600017`/`error=0`）。
- IN 布局实证（`BridgeConnect` 反汇编）：16B=`[param_3,param_5,param_4,param_2]`，取 `[0x80000850,0,0x10000,0]`；桥侧忽略 IN。
- dmesg 1117→1118 行，仅+1 行 `arena close`（fd 关闭正常清理），无 WARN/BUG/Oops；refs（bridge 0/probe 1）不变。
- 未跑 `make probe`（L3）：其 `WITH_BRIDGE` 会 rmmod，违反红线；桥未被扰动，等效健康证据为活体交互全绿。
- 门禁：`check-offline` 400+299 全绿；无代码改动。见 `reports/r358-umd-live-connect.md` + 三证据（0600）。
---

## 本轮进展（r357：UMD 真实建连链路 recon，离线 fabricated）

- 链路语义（语料实测，SHA `b3058c02…34237b0` 对版）：`GetSrvHandle @ 0x3c1c0`=`rdi?*rdi:0`（读连接首 qword；Ghidra 口径 `0x13c1c0`=文件偏移+`0x100000`，与r354/r355 写法统一）；连接 0xd0（`FUN_0013b7c0` 内 `PVRSRVCallocUserModeMem(0xd0)`）；首 qword 由 `OpenServicesDevice`（`FUN_00192550`）写入 0x10 services-handle 指针，其首 dword 为 DRM fd；`PVRSRVBridgeCall`（`FUN_00192930`）`ioctl(*param_1, 0xc0206440)`，`ENOTTY`→`0x26`。
- fabricated 验证（ctypes 直调真实 `.so`，7/7）：dlsym 地址约定交叉核对；`GetSrvHandle` 返回句柄指针（`0x7f…`）；NULL→0；`0x6000` 布局复现 r354 读语义；`BridgeCall(0x82,0xc)` 经 `/dev/null` fd 走 `ENOTTY` 调试路径返回 `0x26`，无崩溃（对比 r354 `0x929ce` SIGSEGV）。
- 设备打开路径盘点：`FUN_001a3ab0`→`FUN_001a4940` 扫描 render minor `0x80–0xbf`，按 driver 名 `"pvr"`/`"mtgpu"` 匹配；后接 `ioctl(0x40046445)`（SRVKM_INIT）+ `BridgeConnect`。
- r358 活体前置与验收已写出（见报告）：freeze 完好 + `renderD128` 存在 + SHA 对版；只建连不提交 GPU 工作；验收=`GetSrvHandle` 指针形态 + 桥侧 `pvr_cmd_connect` 收包 + 建连返回 0。
- 门禁：新增 `tests/test_umd_connection_layout.py`（6 项钉地址/0xd0/读语义/ioctl 号/写链），反向验证通过后还原；`check-offline` 400+299 全绿。零硬件触碰，freeze 继续。
---
---
