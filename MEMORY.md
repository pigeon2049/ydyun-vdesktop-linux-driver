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

最后更新：2026-10-07（r230 双开组合 + r231 多条目混合，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r231：多条目混合 kick，批准执行）

- translator 多条件路径活体走通：双 sync block 各预置一槽后，混合 fire（check=2 + update=1）44.8ms 即过，写回 probe 0.11ms 即过；dmesg `check=2 update=1 fence=9` → `check=1 update=0 fence=10`。工具双 PMR + 门禁更新（含反向）；`check-offline` 325 Python OK。详见 `reports/r231-multi-check-live.md`。
- probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 本次会话进展（r230：双开组合验证，批准执行）

- `=2` + transfer + tqx_ctx 双开同轮三行同现（observe/dry-run 预言一致/ready），两开关正交。UMD 即时退出；拆桥干净，默认 + L3 全绿。附带第六个 `+0x40` 轮变值（`03 a6`）。trace 已入库。详见 `reports/r230-dual-param.md` + `.jsonl`。
- 遗留：TQX 真发射（离线先行）；CCB 解读（离线）；真实执行 backend。USB 短页标题日期问题留待对应轮。

- observer 与 drm_major 正交在活体证实：`=2` 桥上 ping 全套 22 项全 ok，dmesg 三行与 legacy 逐项一致；legacy `0x82:0x8` create 在 `=2` 下同样成功（附带发现）。拆桥干净，默认恢复 + L3 全绿，dmesg 干净。详见 `reports/r229-ddk2-observe-regression.md`。
- **Freeze 已恢复。**无代码改动。
- 遗留：DDK2 param_1 形状 recon（离线）；TQX 真发射；CCB 解读；真实执行 backend。USB 短页标题日期问题留待对应轮。
---
