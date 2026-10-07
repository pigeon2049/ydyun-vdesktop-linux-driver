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

最后更新：2026-10-07（r220 SyncPrimSet 真写离线实现，零硬件触碰）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

## 本次会话进展（r220：SyncPrimSet 真写离线实现，零硬件触碰）

- 开工声明零硬件触碰。“打通卡点”落到值语义链真卡点：`0x2:0x2` 从 stub 改真写（wrapper/生成头/活体三重互证 IN16；复用 translator 解析 + `index*4` 定界 + host 写；无 fence/提交/wakeup）。`0x2:0xd` 仍越界。
- 门禁 7 项新 + `ddk2_render2` 改判 + wire MAPPING 补两行（生成表 16/4 diff 通过）；双重反向验证；`check-offline` 317 Python + 292 C 全绿；`make kernel` W=1 零警告。未加载，会话未碰。详见 `reports/r220-syncprimset-write.md`。
- 遗留：非零值 kick 活体待下轮批准窗口（重载 + raw set + kick 链）。USB 短页标题日期问题留待对应轮。


## 本次会话进展（r219：TQX bring-up 新会话复验，批准执行）

- TQX bring-up 在新会话+新构建上复验通过：`=2` + `translate_tqx_ctx=1`（`translate_transfer` 保持 off）重载，真实 blit 后 `submit3 tqx-ctx: ready`（无 `-22` 回归）；UMD 即时 SIGABRT，无 hanging。probe 1→28→1 对称归零；桥恢复默认 + L3 全绿，dmesg 干净。附带第四个 `+0x40` 轮变值（`60 70`）。trace 已入库。详见 `reports/r219-tqxbringup-reverify.md`。
- **Freeze 已恢复。**
- 遗留：TQX 真发射仍未验证；真实数组流量仍待 producer；真实执行仍待 backend 接线。USB 短页标题日期问题留待对应轮。

---
