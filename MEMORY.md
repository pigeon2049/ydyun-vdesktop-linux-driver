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




## 本轮进展（r359：0x82:0xC 活体观察停轮——在载桥无 observer）

- 基线（真机，freeze 未碰）：refs（bridge 0/probe 1）、`renderD128` 存在、dmesg 无 WARN/BUG/Oops、UMD SHA `b3058c02…34237b0` 对版、HEAD `12c6bd6` 工作区干净。
- 在载桥 build-id `2a2a…261f` ≠ 在盘 r356 构建 `0d6b…55da`（含 observer 串）；在载桥 ~11:26 加载，早于 r356 提交（14:37）约 3 小时；r356/r357/r358 均未重载桥。按任务安全协议 §2 停轮：未发 `0x82:0xC`，未重载桥。
- 门禁 `check-offline` 400+299 全绿；无代码改动。见 `reports/r359-bridge-version-blocked.md` + 证据（0600）。
---

## 本轮进展（r358：UMD 真实建连打通，真机活体）

- 建连（真机活体，纯 userspace，freeze 未碰）：`PVRSRVConnectionCreateDevice(&conn,0xffffffff,0xffffffff)`→0（`conn=0x25220fe0`，1ms），经 `_GetFd` 打开 `renderD128`（driver `pvr` 首轮匹配）→`ioctl(0x40046445)`→内部 `BridgeConnect`→`GetFeatures`（纯读）→`BridgeAlignmentCheck(1,0xa)`（桥 `pvr_stub_ok` 回零）。
- `GetSrvHandle(conn)`→`0x252211a0` 指针形态；单次显式 `PVRSRVBridgeCall(1,0,in16,out17)`→0，OUT 逐字节命中桥侧 `mt_pvr_connect_result`（`bvnc=0x0023000406600017`/`error=0`）。
- IN 布局实证（`BridgeConnect` 反汇编）：16B=`[param_3,param_5,param_4,param_2]`，取 `[0x80000850,0,0x10000,0]`；桥侧忽略 IN。
- dmesg 1117→1118 行，仅+1 行 `arena close`（fd 关闭正常清理），无 WARN/BUG/Oops；refs（bridge 0/probe 1）不变。
- 未跑 `make probe`（L3）：其 `WITH_BRIDGE` 会 rmmod，违反红线；桥未被扰动，等效健康证据为活体交互全绿。
- 门禁：`check-offline` 400+299 全绿；无代码改动。见 `reports/r358-umd-live-connect.md` + 三证据（0600）。
---

---
---
