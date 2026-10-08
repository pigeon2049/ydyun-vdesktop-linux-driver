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




## 本轮进展（r360：mt_pvr_bridge 重载至 r356 构建成功，真机活体）

- 换桥（真机活体，用户已批准 2026-10-08 15:13）：预检（fuser 无持有者、bridge ref 0、probe ref 1、vermagic 一致、observer 串 grep=1）→ `rmmod mt_pvr_bridge` → `insmod` r356 构建。新桥 build-id `0d6b…55da` == 在盘构建 ≠ 旧 `2a2a…261f`。
- dmesg：`unloaded cleanly` → `[drm] Initialized pvr 0.1.0 ... on minor 1` → `registered 'pvr' node`；无新增 WARN/BUG/Oops。
- 健康检查 PASS（纯 userspace，r358 方法）：`PVRSRVConnectionCreateDevice`→0、`GetSrvHandle` 指针形态、单次 `PVRSRVBridgeCall(1,0)`→0 且 OUT 逐字节命中（`bvnc=0x0023000406600017`/`error=0`）。
- 方法教训：`/proc/self/maps` 取 load bias 须减 file offset；`PVRSRVBridgeCall` 真签名 7 参数 `(handle,bridge,func,in_ptr,in_len,out_ptr,out_len)`（connect 实证 r8d=0x10/stack=0x11）。
- freeze 恢复确认：probe 仍绑 00:0e.0（ref 1，未碰）、bridge 默认参数在载（ref 0）、card0/card1/renderD128 齐全。见 `reports/r360-bridge-reload-r356.md` + 四证据（0600）。
---
## 本轮进展（r359：0x82:0xC 活体观察停轮——在载桥无 observer）

- 基线（真机，freeze 未碰）：refs（bridge 0/probe 1）、`renderD128` 存在、dmesg 无 WARN/BUG/Oops、UMD SHA `b3058c02…34237b0` 对版、HEAD `12c6bd6` 工作区干净。
- 在载桥 build-id `2a2a…261f` ≠ 在盘 r356 构建 `0d6b…55da`（含 observer 串）；在载桥 ~11:26 加载，早于 r356 提交（14:37）约 3 小时；r356/r357/r358 均未重载桥。按任务安全协议 §2 停轮：未发 `0x82:0xC`，未重载桥。
- 门禁 `check-offline` 400+299 全绿；无代码改动。见 `reports/r359-bridge-version-blocked.md` + 证据（0600）。
---
