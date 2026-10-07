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

最后更新：2026-10-07（r229 observer DDK2 回归验证，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r229：observer DDK2 回归验证，批准执行）

- observer 与 drm_major 正交在活体证实：`=2` 桥上 ping 全套 22 项全 ok，dmesg 三行与 legacy 逐项一致；legacy `0x82:0x8` create 在 `=2` 下同样成功（附带发现）。拆桥干净，默认恢复 + L3 全绿，dmesg 干净。详见 `reports/r229-ddk2-observe-regression.md`。
- **Freeze 已恢复。**无代码改动。
- 遗留：DDK2 param_1 形状 recon（离线）；TQX 真发射；CCB 解读；真实执行 backend。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r228：DDK2 SetSyncPrim 侦察，批准执行）

- DDK2 下 `SetSyncPrim` 在 UMD 内 SIGSEGV（exit 139），`0x2:0xd` 从未发出；dmesg+GDB 双定 RVA `0xa0b38`，DDK2 分支把 param_1 当 device 上下文解 `[0]` 而 harness 传的 connect 派生 context 该槽是桥 VA 常量 → 野读。非桥缺口，合法 param_1 形状待 recon。trace 已入库。详见 `reports/r228-ddk2set-segv.md` + `.jsonl`。
- 拆桥干净，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**无代码改动。
- 遗留：DDK2 param_1 形状 recon（离线）；TQX 真发射；CCB 解读；真实执行 backend。USB 短页标题日期问题留待对应轮。
---
