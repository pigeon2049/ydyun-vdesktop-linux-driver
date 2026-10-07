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

最后更新：2026-10-07（r225 UMD 生成 CCB 进真桥观察，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r225：UMD 生成 CCB 进真桥观察，批准执行）

- fabricated UMD 链与真桥 observer 闭环：CCB 相从 r210 bin 逐槽载入 61 非零槽到 0x4700 窗口，以 UMD 自身 VA/size/ID/counts fire；桥报 nonzero=107/FNV/head 与离线预言逐项一致（head64 另行逐字节比对全等）。22 项全 ok；refs 不变（默认桥，无需重载），dmesg 干净。门禁 +1（含有效反向）。`check-offline` 325 Python OK。详见 `reports/r225-ccb-observe-live.md`。
- **Freeze 继续。**
- 遗留：CCB 内容解读（离线）；TQX 真发射；真实执行 backend。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r224：observer 非零窗口活体验证，批准执行）

- observer 非零路径活体走通：零窗口 fire 后以 `0x2:0xa` 预置 5×u32 再 fire，桥报 nonzero=20/FNV/head 与开工前离线预言逐项一致。13 项全 ok；refs 不变（默认桥，无需重载），dmesg 干净。门禁 +1（含一次无效反向后的有效反向）。`check-offline` 324 Python OK。详见 `reports/r224-observe-nonzero-live.md`。
- **Freeze 继续。**
- 遗留：CCB 内容解读（离线）；TQX 真发射；真实执行 backend。USB 短页标题日期问题留待对应轮。
---
