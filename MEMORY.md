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

最后更新：2026-10-07（r216 新构建上机 + L3，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r216：新构建上机 + L3，批准执行）

- 积压的 6 个本地提交已 push（`104fdeb..fff3769`）。r215 新构建（含 `0x82:0x14` observer）上机：装盘前验 strings + vermagic；单桥重载（probe 未碰），节点仍 `renderD128`；L3 全绿（node + smoke，refs 平衡），终态 ref 1/0，dmesg 零 WARNING/BUG/Oops。详见 `reports/r216-newbuild-reload.md`。
- observer 已在载但尚无真实流量（parked，不是 proven）；路由活体 ping 需新工具代码，留待下轮。**Freeze 已恢复。**
- 遗留：真实 GFX producer 仍 open；真实执行仍待 backend 接线。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r215：`0x82:0x14` observer 离线实现，零硬件触碰）

- 开工声明零硬件触碰。STATUS #2 的 concrete 缺口（r190）闭合一半：新增 `pvr_cmd_kickta3d5_observe`（r174 模式：108B 定界 + render 上下文鉴权 + VA→reservation→PMR 三重定界 + 非零/FNV/head 统计 + 标量上报回 0，不读嵌套指针、不执行、无 fence）；分发接 `case MT_PVR_FN_RGXKICKTA3D5`（wire.h 新宏，r188 惯例）。
- 门禁 7 项新 + `fn_ids` 55→56，反向掐断验证通过；`check-offline` 302 Python + 292 C 全绿；`make kernel` W=1 零警告。未加载模块（在载桥仍旧构建，observer 待批准窗口重载验证）。详见 `reports/r215-kickta3d5-observer.md`。
- 活体 GFX kick 方向止损：无现成生产者脚本，翻炒 GDB 手塑链风险收益不成正比；真实 3D producer 仍 open（r190）。
- 遗留：真实执行仍待 backend 接线；update 活体待重载窗口。USB 短页标题日期问题留待对应轮。

---
