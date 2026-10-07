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


## 本轮进展（r285：fire 模块 soak 全绿，批准执行）

- 5 轮装/打/卸：轮轮 `fired=Y verified=Y chunks=21 result=0`，probe/bridge ref 轮轮 1→1，节点现即消；窗口零新增 WARN。无代码改动。
- **bridge 未碰，freeze 继续。**
- 遗留：合并策略 recon。本地提交仍未 push。
---


## 本轮进展（r286：legacy 基线复核，无重载）

- 当前构建+会话真实 blit：101 调用仅 `0x89:0x0` → -25（seq 8180），有序拆除，UMD 134；与 r244 同形，拒绝点复核成立。refs 全程 1/1，窗口零新增 WARN。无代码改动。
- **freeze 继续。**
- 遗留：合并策略 recon。本地提交仍未 push。
---


## 本轮进展（r287：上限边界 64/64 全绿，批准执行）

- 4096×1024（16 行/块恰顶 64 上限）：`fired=1 chunks=64 verified=1 bad=0/4194304`；拆模块干净，refs 回 1/1，窗口零新增 WARN。无代码改动。
- **bridge 未碰，freeze 继续。**独立模块执行侧已无未验证项（3 几何 + 3 颜色 + 5 轮 soak）。
- 遗留：合并策略 recon。本地提交仍未 push。
---


## 本轮进展（r288：合并策略 recon，离线决策）

- 决定：模块转正式工具不再为合并改动；桥 r280 fire 保留（默认关）并记流水线自阻塞 defect（合流前须串行移植）；holder 解锁程序明确（stop service，需用户协调）。纯文档，无代码改动。
- 遗留：桥 fire 串行移植 + holder 窗口（动桥前提）；快照 §5 缺陷落盘攒刷新 pass。本地提交仍未 push。
---


## 本轮进展（r289：超限 `-E2BIG` 活体拒绝，批准执行）

- 3840×2160（128 块）：`fired=0 chunks=128 result=-7`，零提交零像素触碰；拆模块干净，refs 回 1/1，窗口零新增 WARN。模块正反分支活体全覆盖，无代码改动。
- **bridge 未碰，freeze 继续。**
- 遗留：holder 窗口（UMD 驱动 fire 唯一剩余路径）。本地提交仍未 push。
---


## 本轮进展（r290：UMD 驱动 fire 首绿，批准执行）

- 桥 fire 串行移植（离线）：handler 定位+调度，work 逐块执行；门禁改判 13 项 + 反向 + 366+292 全绿 + W=1 零警告。停桌面窗口 + 三开 + 真实 blit：UMD 矩形 1280×1024 `fired=1 chunks=21 verified=1 bad=0/1310720`，STATUS #1 真实绘制打通。
- 恢复曲折：脚本 grep 缺 `-a` 误判 TIMEOUT；用户手动重开桌面致 rmmod 被拒（r291 订正：非自重启）；手动补恢复关账（probe 31→1，L3 全绿，桌面拉回）。窗口零新增 WARN。脚本两 bug 已修。
- 教训：窗口约束是用户容忍度（90 秒定律作废）；CCB nonzero=40（+1 未命名）。**Freeze 已恢复。**
- 遗留：TA/3D CCB 与 update 的 UMD 驱动验证（用户协调窗口）。本地提交仍未 push。
---


## 本轮进展（r291：UMD 驱动 fire 复现全绿 + r290 订正，批准执行）

- 第二窗口零干预全绿：UMD 矩形二次 `fired=1 chunks=21 verified=1`；L3 双绿；终态 refs 1/1，窗口零新增 WARN。CCB 第三样本 nonzero=40（轮值第 9 值 `dd 35`，+1 仍未命名）。
- r290 订正（用户指正）：两次“重启”均为手动重开，无自重启证据；90 秒定律作废，约束为用户容忍度。脚本时限收紧（60/60）+ LC_ALL/unset 修已验证生效（本轮 trace 零污染、fired 一次命中）。
- **Freeze 已恢复。**
- 遗留：TA/3D CCB 或 update 的 UMD 驱动验证（需离线 recon producer）。本地提交仍未 push。
---


## 本轮进展（r292：同寿命双发全绿，批准执行）

- 第三窗口零干预：seq=1/seq=2 背靠背 `fired=1 chunks=21 verified=1`（单发复位成立）；L3 双绿；终态 refs 1/1，窗口零新增 WARN。CCB 第四/五样本 nonzero=40（轮值第 10/11 值）。脚本多轮化（BLITS/WINID）。
- **Freeze 已恢复。**
- 遗留：TA/3D 或 update 的 UMD 驱动验证（需离线 recon producer；blit submit3 后 hanging 是前置山）。本地提交仍未 push。
---

