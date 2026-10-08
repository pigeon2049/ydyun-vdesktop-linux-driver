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




## 本轮进展（r362：r361 描述子"不匹配"系 GDB 脚本读错位置）

- `CreateSyncPrim` 反汇编确认：描述子 `+0x18` = `RA_Alloc` 的 `puStack_70` 输出，写入 `*param_2`（`b10`）；r361 GDB 脚本在入口取 `$rdi`（param_1）、返回时读 `*(param_1)`——读错了位置。
- 实测（同一 harness，两次独立运行）：`*(param_2)` 处 `+0x18=<ptr>、+0x20=0`（= r352/r353）；`*(param_1)` 处 `+0x18=NULL、+0x20=<heap ptr>`（= r361 dump）。直接调用 `SyncPrimRef(*b10)` → 0。
- 定性：测量方法 bug，非输入/状态/布局问题。无需 fabricated 参数调整。r363 唯一前置：修正 GDB 脚本从 `$rsi` 取 param_2。零硬件触碰，freeze 完好。见 `reports/r362-desc-mismatch-root-cause.md` + 双证据（0600）。
---
## 本轮进展（r360：mt_pvr_bridge 重载至 r356 构建成功，真机活体）

- 换桥（真机活体，用户已批准 2026-10-08 15:13）：预检（fuser 无持有者、bridge ref 0、probe ref 1、vermagic 一致、observer 串 grep=1）→ `rmmod mt_pvr_bridge` → `insmod` r356 构建。新桥 build-id `0d6b…55da` == 在盘构建 ≠ 旧 `2a2a…261f`。
- dmesg：`unloaded cleanly` → `[drm] Initialized pvr 0.1.0 ... on minor 1` → `registered 'pvr' node`；无新增 WARN/BUG/Oops。
- 健康检查 PASS（纯 userspace，r358 方法）：`PVRSRVConnectionCreateDevice`→0、`GetSrvHandle` 指针形态、单次 `PVRSRVBridgeCall(1,0)`→0 且 OUT 逐字节命中（`bvnc=0x0023000406600017`/`error=0`）。
- 方法教训：`/proc/self/maps` 取 load bias 须减 file offset；`PVRSRVBridgeCall` 真签名 7 参数 `(handle,bridge,func,in_ptr,in_len,out_ptr,out_len)`（connect 实证 r8d=0x10/stack=0x11）。
- freeze 恢复确认：probe 仍绑 00:0e.0（ref 1，未碰）、bridge 默认参数在载（ref 0）、card0/card1/renderD128 齐全。见 `reports/r360-bridge-reload-r356.md` + 四证据（0600）。
---
