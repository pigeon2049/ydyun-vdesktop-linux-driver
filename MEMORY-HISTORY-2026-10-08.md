# MEMORY-HISTORY-2026-10-08（只读归档；原样移入，不回改）

## 本次会话进展（r279：64 页绕行验证，批准执行）

- Chrome 关闭后重载验证：`=2` + tqx_ctx 下 slices ready + tqx-ctx ready 全现（bind 在 64 页下通过，2112 黑盒绕过）；blit hanging 60s 被杀系 UMD 行为（无 D 态，可 rmmod，对称归零）。拆桥干净，默认 + L3 全绿；本轮窗口零 WARN。trace 已入库。详见 `reports/r279-space64-live.md` + `.jsonl`。
- **Freeze 已恢复。**无代码改动（r278 离线部分已在盘）。
- 遗留：fire 分块循环（离线）；分块 fired/verified 活体。USB 短页标题日期问题留待对应轮。
---

## 本轮进展（r280：fire 分块循环离线实现，零硬件触碰）

- 接盘盘内半成品（struct 数组化、submit/teardown 仍单 fence，构建已破坏）并收尾：全帧按 51 行/块拆 21 块（上限 64），复用 scratch 基址，21 fence 逐序等、验尾块内容；teardown 全放；中途失败全 put。
- 门禁：`test_pvr_tqx_fire.py` +3（切条/全等/全放），反向验证通过（首轮改宏名后缀因被子串包含未触发，作废记教训；删 `chunks=%u` 即 2 FAIL，还原即绿）。`check-offline` 354+292 全绿；`make kernel` W=1 零警告。
- **Freeze 继续（未加载，会话未碰）。**
- 遗留：分块 fired/verified 活体（需批准：`=2` + tqx_ctx + fire 三开 + 真实 blit，看 `chunks=21` + `verified=1`）。USB 短页标题日期问题留待对应轮。
---

## 本轮进展（r282：fire 独立模块离线实现，零硬件触碰）

- r281 绕行：新模块 `mt_live_tqx_fire` 自有 render 节点（`mtlivefire`，零 ioctl）+ 自有 64 页 space/TQX ctx/slices/scratch，直连 probe 会话，不碰 bridge/renderD128。分块 fire（21 块/21 fence/验尾块），root-only 单发，失败行号上报，锁序沿 r265。
- 门禁 9 新项（含反向；修门禁自身 `cls.run` 覆盖 bug）。`check-offline` 363+292 全绿；`make kernel` W=1 零警告。
- **Freeze 继续（未加载，会话未碰）。**
- 遗留：独立模块活体（需批准：insmod 不碰 bridge → 新节点 + fuser 空 → run → dmesg → rmmod）。USB 短页标题日期问题留待对应轮。
---

## 本轮进展（r281：分块 fire 活体被持有挡回，批准执行，未触硬件）

- 三开重载第一步即被拒：`renderD128` 被 PID 60651（会话桌面自身）持有，bridge ref 1，`rmmod` 报 `in use` 即停手（未 `-f`、未 insmod、无提交）。refs 仍 1/1，本轮窗口零新增 WARN。空暂存区 `build/traces/r281/`。
- 遗留：holder 释放（需用户侧，agent 关不掉自家桌面）后重跑三开验证（`chunks=21` + `verified=1`）；r280 代码在盘未上机。2 提交仍在本地未 push（用户选择暂不推送）。
---


## 本轮进展（r283：独立模块首次真发射全绿，批准执行）

- 自有节点 bring-up 一次过（`prepared=1` + `slices: ready` + `card2`/`renderD129`）；活体三处反馈即修：completed 门删除（终身计数≠就绪）/串行流（slices 外借语义）/verify 补 `upload_dev`。终轮 `fired=1 chunks=21 verified=1 bad=0/1310720 result=0`。
- 拆模块干净（节点消失，refs 回 1/1）；窗口零新增 WARN。门禁 366+292 全绿；`make kernel` 零警告；反向验证通过。
- **Freeze 已恢复（bridge/probe/renderD128 全程未碰）。**
- 遗留：合并策略 recon（fire 合进 bridge vs 保持独立）或更大矩形/soak。本地提交仍未 push。
---


## 本轮进展（r284：大矩形分块通用性活体，批准执行）

- 换参（1920×1080/`0xff00ff00`）重载 fire 模块：`fired=1 chunks=32 verified=1 bad=0/2073600`，与离线预言（34 行/块、32 块）逐项一致；颜色参数真实落地。拆模块干净，refs 回 1/1，窗口零新增 WARN。无代码改动。
- **bridge 未碰，freeze 继续。**
- 遗留：soak 重复性或合并策略 recon。本地提交仍未 push。
---

