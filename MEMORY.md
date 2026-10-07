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

最后更新：2026-10-07（r207 KickTA3D5 wire 与执行链盘点）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r207：KickTA3D5 wire 固定与执行链盘点）

- 零硬件触碰。加入 hash 对版 5.2 `MUSAKICKGFX5` 的 108/4 wire 结构与偏移静态断言；gate 映射到 `0x82:0x14` UMD size row，未接 dispatch。
- 执行链盘点：render2 context 只是 handle token；sync handle 可到 PMR；现有 kick translator 只发固定 marker，TDM Submit3 只做窗口观察/dry-run。没有能执行 UMD TA/3D CCB 的完整 backend。
- 验证：`make check-offline` 全绿（295 Python、1 skip、292 C）；`make kernel` W=1 零警告。
- 证据：`reports/r207-kickta3d5-wire-foundation.md`。后续需接真实 context、nested sync/PMR arrays 和 TA/3D completion；live 仍待明确批准。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r206：KickGFX5 Host schema 映射）

- 零硬件触碰。对版 5.2 DKMS 包 SHA=`e3f684b1…` 的生成头将 `0x82:0x14` 映射到 `MUSAKICKGFX5 +20`；schema 的 flags/VA/size/submissionID/count 偏移和值逐项匹配 r203 fabricated trace。
- 12B 偏移差现已由额外 flags 与 submissionID 字段解释；同包 `MTGPUMUSAGFX5KM` 声明给出 sync/PMR 数组与提交参数接口顺序，但没有 handler 实现体；下一步核对 Guest handle 台账和实际 CCB 执行路径。
- 证据：`reports/r206-kickgfx5-schema.md`、r203 trace、`downloads/mthreads-dkms_5.2.0_amd64.deb`。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

---

---
