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

最后更新：2026-10-07（r224 observer 非零窗口活体验证，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r224：observer 非零窗口活体验证，批准执行）

- observer 非零路径活体走通：零窗口 fire 后以 `0x2:0xa` 预置 5×u32 再 fire，桥报 nonzero=20/FNV/head 与开工前离线预言逐项一致。13 项全 ok；refs 不变（默认桥，无需重载），dmesg 干净。门禁 +1（含一次无效反向后的有效反向）。`check-offline` 324 Python OK。详见 `reports/r224-observe-nonzero-live.md`。
- **Freeze 继续。**
- 遗留：UMD 真实 CCB 进 observer；TQX 真发射；真实执行 backend。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r223：update 写回活体验证，批准执行）

- translator 写回语义（r159）活体闭环：新工具 `pvr_update_writeback` 在 `translate_kick=1` 桥上，update-only fire 写 V=1 回 0，check kick 0.11ms 即时通过（时间即读回）；dmesg 双行 fence=5/6。工具零警告构建 + 5 项门禁（含反向）。详见 `reports/r223-update-writeback-live.md`。
- probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**`check-offline` 323 Python OK。
- 遗留：UMD 生成的真实 update 数组仍待 producer；TQX 真发射；真实执行 backend。USB 短页标题日期问题留待对应轮。

---
