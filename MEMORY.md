# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](MEMORY-HISTORY-2026-10-07.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-07（r228 DDK2 SetSyncPrim 侦察，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r228：DDK2 SetSyncPrim 侦察，批准执行）

- DDK2 下 `SetSyncPrim` 在 UMD 内 SIGSEGV（exit 139），`0x2:0xd` 从未发出；dmesg+GDB 双定 RVA `0xa0b38`，DDK2 分支把 param_1 当 device 上下文解 `[0]` 而 harness 传的 connect 派生 context 该槽是桥 VA 常量 → 野读。非桥缺口，合法 param_1 形状待 recon。trace 已入库。详见 `reports/r228-ddk2set-segv.md` + `.jsonl`。
- 拆桥干净，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**无代码改动。
- 遗留：DDK2 param_1 形状 recon（离线）；TQX 真发射；CCB 解读；真实执行 backend。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r227：混合 kick 活体验证，批准执行）

- translator 完整语义（r159）活体闭环：预置 check 槽 V7 后混合 fire（check+update）44.8ms 即过（与 r222 Leg1 同构），update 写回 probe 0.11ms 即过；dmesg 双行 `check=1 update=1 fence=7` → `check=1 update=0 fence=8`。工具升级三相 + 门禁更新（含反向）；`check-offline` 325 Python OK。详见 `reports/r227-mixed-kick-live.md`。
- probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**
- 遗留：TQX 真发射（离线先行）；CCB 内容解读（离线）；真实执行 backend。USB 短页标题日期问题留待对应轮。
---
