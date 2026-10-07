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

最后更新：2026-10-07（r208 PMR 到真实 GPU VM 的后端边界）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r208：PMR 到真实 GPU VM 的后端边界）

- 零硬件触碰。只读确认 `mt_bo_system_borrow()` 能将稳定 `mt_system_memory` 的逐页 GPA 包成设备 session BO；VM 绑定要求 BO 与页表 BO 的 store/ops 一致。现有 PVR PMR `gpu_bo` 是 CPU-only planning facade，不能进入真实 VM。
- 真实接线仍需 per-file 上传 VM、process/render context、PMR borrowed BO 生命周期、nested sync/PMR 解引用、CCB 资源闭包和 fence 完成。当前 translator 全局共享、context 只是 token；marker/TDM observer 不执行真实 CCB。
- 未改代码、未跑门禁、未动硬件。设计路线和证据边界见 `reports/r208-ddk2-render-backend-boundary.md`。STATUS 下一步不变。
- 遗留：真实 CCB 活体验证仍须用户明确批准；执行包格式/资源闭包尚未证实。

---

## 本次会话进展（r207：KickTA3D5 wire 固定与执行链盘点）

- 零硬件触碰。加入 hash 对版 5.2 `MUSAKICKGFX5` 的 108/4 wire 结构与偏移静态断言；gate 映射到 `0x82:0x14` UMD size row，未接 dispatch。
- 执行链盘点：render2 context 只是 handle token；sync handle 可到 PMR；现有 kick translator 只发固定 marker，TDM Submit3 只做窗口观察/dry-run。没有能执行 UMD TA/3D CCB 的完整 backend。
- 验证：`make check-offline` 全绿（295 Python、1 skip、292 C）；`make kernel` W=1 零警告。
- 证据：`reports/r207-kickta3d5-wire-foundation.md`。后续需接真实 context、nested sync/PMR arrays 和 TA/3D completion；live 仍待明确批准。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---
