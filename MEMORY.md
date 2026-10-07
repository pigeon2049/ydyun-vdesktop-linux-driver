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

最后更新：2026-10-07（r212 check-only 新会话复验，批准执行）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r212：check-only 新会话复验，批准执行）

- r211 新会话上 legacy check-only 复验通过：桥以 `translate_kick=1` 重载（probe 未碰），r73 配方首跑全绿（check 值直接用 r148 实测值 0），六符号全 0、`0x88:0x4 ret=0`，dmesg `translated kick: check=1 update=0 tag=1 fence=1`（与 r148 同形）。
- 拆桥 `unloaded cleanly`，probe ref 25→1；桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。trace 130 行已入库 `reports/r212-checkonly-reverify.jsonl`（不再放易失 `/tmp`）。详见 `reports/r212-checkonly-reverify.md`。
- **Freeze 已恢复**：不 rmmod、不 unbind、不提交额外工作。
- 遗留：DDK2 check-only（r149 对应项）仍待复验；真实绘制 CCB 仍待 backend 接线。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r211：活体会話重建，批准执行）

- 本机重启进 `6.12.111`，旧 r166 会话消失（`00:0e.0` 无绑定、无模块在载）。用户批准真机测试后重建：`cold_disconnect finish=0/1` 均 rings idle、`guest=0 firmware=1`、双 clean rmmod；`fresh-trial.py --run --runtime-context` rc=0，新 trial `20261007T040408Z-f3fb55af`（firmware sha `35d40f75…` 与 r138/r166 一致，Guest/FW `2/2` pinned，ref 1）。
- `mt_pvr_bridge.ko` 默认参数加载（`card1`/`renderD128`，ref 0）；L3 全绿（node 0 failing/0 mismatch，dma smoke PASS，refs 1→2→1，无 GPU 提交）；dmesg 无新增 WARN/BUG/Oops。`make kernel` + `kernel/recovery` W=1 零警告。
- **Freeze 即刻生效**：不 rmmod、不 unbind、不提交额外工作。详见 `reports/r211-session-rebuild.md`。
- 遗留：真实绘制 CCB 活体验证（STATUS 下一步 #1）与 update 语义活体验证（#2）仍待真实 DDK2 render backend 接线；r210 fabricated CCB 仍是离线字节。USB/画面线短页标题日期仍为 2026-10-03，留待对应轮处理。

---
