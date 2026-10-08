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



## 本轮进展（r356：0x82:0xC wire 入库 + observer 占位，离线）

- 入库：`kernel/mt_pvr_wire.h` 新增 `mt_pvr_musakickgfx2_in`（268B）/`_out`（12B），字段与 5.2 生成头 1:1；类型尺寸经 5.2 DKMS 包（SHA `e3f684b1…`）实证：`MTGPU_FENCE`/`MTGPU_TIMELINE`=`int32_t`，`MT_BOOL`=4B 枚举，`MT_HANDLE`=8B；268/12 与 requirements 表既有 `0x82:0xC=RGXKICKTA3D2` 条目逐字节一致。
- 占位：`pvr_cmd_musakickgfx2_observe()` 接入 dispatch，解码打印标量头字段后返 `-ENOTTY`（明确非执行；与 0x82:0x14 的 accept-and-log 区分，守 STATUS 红线）。
- 门禁：static_assert 钉尺寸+7 偏移；`test_pvr_wire_sizes.py` MAPPING/DIRECTION 新增 `(0x82,0xC)`；`check-offline` 394+299 全绿；反向验证（267→编译失败）通过后还原；`make kernel` W=1 零警告。
- 遗留：r357（UMD 真实建连 recon）。零硬件触碰，freeze 继续。
---

## 本轮进展（r355：DDK2 render backend 缺口盘点，离线）

- 盘点：桥侧 dispatch 三分法——已真实实现（SRVCORE/SYNC/MM、0x88:0x4 翻译、TQX fire、MUSAKICKGFX5 schema）；accept-and-log 空桩（0x82:0x14、0x89:0xa、0x82:0x12/0x88:0x5、0x89:0x8/0x9）；缺失（0x82:0xC、TA firmware 提交通道、per-file VM/BO、UMD 真实建连）。
- 新发现：r354 证据 `0x92930(rdi=0x6000,rsi=0x82,rdx=0xc)` 表明 UMD TA 路径内实际发出 `0x82:0xC`；对照 5.2 生成头为 MUSAKICKGFX2（TA/3D/PR 提交富结构），桥侧无定义、走 `-ENOTTY`。STATUS 口径修正：0x82:0xC 从"S4 边界"升级为具体可排的下一个桥目标；0x81:0x5 维持边界。
- 需求清单 R1–R7：UMD 真实建连（`PVRSRVConnectionCreateDevice`，0xd0 结构；`GetSrvHandle @ 0x13c1c0`=`rdi?*rdi:0`，SHA 对版；`PVRSRVBridgeCall` 即 `FUN_00192930`，ioctl 号 `0xc0206440` 与桥侧 static_assert 同值）→ 0x82:0xC 实现 → 0x82:0x14 执行 → TA 提交通道 → per-file VM/BO（r208）→ DDK2 context 状态 → sync prim 导入验证。
- 分轮分解 r356+（提案）：r356=0x82:0xC wire 入库+observer 占位；r357=UMD 真实建连 recon；r358=0x82:0xC 活体观察（需批准）；r359=TA 提交通道设计；r360=0x82:0x14 执行翻译设计；r361+=实现验收。
- 遗留：r356（0x82:0xC wire 入库）。零硬件触碰，freeze 继续。
---
