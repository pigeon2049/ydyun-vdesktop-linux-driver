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



## 本轮进展（r357：UMD 真实建连链路 recon，离线 fabricated）

- 链路语义（语料实测，SHA `b3058c02…34237b0` 对版）：`GetSrvHandle @ 0x3c1c0`=`rdi?*rdi:0`（读连接首 qword；Ghidra 口径 `0x13c1c0`=文件偏移+`0x100000`，与r354/r355 写法统一）；连接 0xd0（`FUN_0013b7c0` 内 `PVRSRVCallocUserModeMem(0xd0)`）；首 qword 由 `OpenServicesDevice`（`FUN_00192550`）写入 0x10 services-handle 指针，其首 dword 为 DRM fd；`PVRSRVBridgeCall`（`FUN_00192930`）`ioctl(*param_1, 0xc0206440)`，`ENOTTY`→`0x26`。
- fabricated 验证（ctypes 直调真实 `.so`，7/7）：dlsym 地址约定交叉核对；`GetSrvHandle` 返回句柄指针（`0x7f…`）；NULL→0；`0x6000` 布局复现 r354 读语义；`BridgeCall(0x82,0xc)` 经 `/dev/null` fd 走 `ENOTTY` 调试路径返回 `0x26`，无崩溃（对比 r354 `0x929ce` SIGSEGV）。
- 设备打开路径盘点：`FUN_001a3ab0`→`FUN_001a4940` 扫描 render minor `0x80–0xbf`，按 driver 名 `"pvr"`/`"mtgpu"` 匹配；后接 `ioctl(0x40046445)`（SRVKM_INIT）+ `BridgeConnect`。
- r358 活体前置与验收已写出（见报告）：freeze 完好 + `renderD128` 存在 + SHA 对版；只建连不提交 GPU 工作；验收=`GetSrvHandle` 指针形态 + 桥侧 `pvr_cmd_connect` 收包 + 建连返回 0。
- 门禁：新增 `tests/test_umd_connection_layout.py`（6 项钉地址/0xd0/读语义/ioctl 号/写链），反向验证通过后还原；`check-offline` 400+299 全绿。零硬件触碰，freeze 继续。
---


## 本轮进展（r356：0x82:0xC wire 入库 + observer 占位，离线）

- 入库：`kernel/mt_pvr_wire.h` 新增 `mt_pvr_musakickgfx2_in`（268B）/`_out`（12B），字段与 5.2 生成头 1:1；类型尺寸经 5.2 DKMS 包（SHA `e3f684b1…`）实证：`MTGPU_FENCE`/`MTGPU_TIMELINE`=`int32_t`，`MT_BOOL`=4B 枚举，`MT_HANDLE`=8B；268/12 与 requirements 表既有 `0x82:0xC=RGXKICKTA3D2` 条目逐字节一致。
- 占位：`pvr_cmd_musakickgfx2_observe()` 接入 dispatch，解码打印标量头字段后返 `-ENOTTY`（明确非执行；与 0x82:0x14 的 accept-and-log 区分，守 STATUS 红线）。
- 门禁：static_assert 钉尺寸+7 偏移；`test_pvr_wire_sizes.py` MAPPING/DIRECTION 新增 `(0x82,0xC)`；`check-offline` 394+299 全绿；反向验证（267→编译失败）通过后还原；`make kernel` W=1 零警告。
- 遗留：r357（UMD 真实建连 recon）。零硬件触碰，freeze 继续。
---
