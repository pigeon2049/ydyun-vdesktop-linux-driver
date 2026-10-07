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

最后更新：2026-10-07（r211 活体会話重建，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r211：活体会話重建，批准执行）

- 本机重启进 `6.12.111`，旧 r166 会话消失（`00:0e.0` 无绑定、无模块在载）。用户批准真机测试后重建：`cold_disconnect finish=0/1` 均 rings idle、`guest=0 firmware=1`、双 clean rmmod；`fresh-trial.py --run --runtime-context` rc=0，新 trial `20261007T040408Z-f3fb55af`（firmware sha `35d40f75…` 与 r138/r166 一致，Guest/FW `2/2` pinned，ref 1）。
- `mt_pvr_bridge.ko` 默认参数加载（`card1`/`renderD128`，ref 0）；L3 全绿（node 0 failing/0 mismatch，dma smoke PASS，refs 1→2→1，无 GPU 提交）；dmesg 无新增 WARN/BUG/Oops。`make kernel` + `kernel/recovery` W=1 零警告。
- **Freeze 即刻生效**：不 rmmod、不 unbind、不提交额外工作。详见 `reports/r211-session-rebuild.md`。
- 遗留：真实绘制 CCB 活体验证（STATUS 下一步 #1）与 update 语义活体验证（#2）仍待真实 DDK2 render backend 接线；r210 fabricated CCB 仍是离线字节。USB/画面线短页标题日期仍为 2026-10-03，留待对应轮处理。


## 本次会话进展（r210：fabricated GFX 原始 CCB 捕获）

- 零硬件触碰。恢复重放工作目录与 sync tuple 后，GDB 实测 check/update helper 均返回 0，`RGXKickGfx` 返回 0；trace seq 125 发出 `0x82:0x14`，VA=`0x8000023000`、size=`0x4700`、check/update counts=1。
- r209 shim 落盘 18,176 字节原始 UMD CCB，107 字节非零，SHA-256=`faa93985aa6b3f65df66641ac73f25020a66fca7af6cdece3b9e88c3a315dae7`。保存于 `reports/r210-gfx-ccb-capture.bin`，桥 trace 在同名 `.jsonl`。
- 不证明 Guest handler 或 GPU 执行；STATUS 下一步仍是补齐真实 DDK2 render backend 和 TA/3D CCB 执行链。详见 `reports/r210-gfx-ccb-capture.md`。
- 遗留：USB/画面线短页 `docs/PROGRESS.md` 标题日期仍为 2026-10-03；本轮是 vGPU 任务，留待对应短页刷新轮处理。

---
