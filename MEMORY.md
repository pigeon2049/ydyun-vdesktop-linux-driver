# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-04（r133 非零 CCB create 开关无差异；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r133：非零 CCB create + ddk_feature_set=2，仍无差异；批准执行）

- 按 r76/r77 还原 r78 命令（pack 0x0733），仅 create+destroy 不 kick；
  `=2` 与默认各重载各跑，桥调用 91=91 逐项一致，无 SyncPrim/SubmissionBuf。
- 模块停在默认参数新桥，引用 0/113，无新 WARN。
- 下一步（离线）：语料核 `RGXCreateKickSyncContextCCB@0x52180` 门控读取点，别再盲重载。
- 证据：`mt-vgpu-guest/reports/r133-ddk2-ccb-create-live.md`。遗留：70 提交未 push。

---

## 本次会话进展（r132：ddk_feature_set=2 活体，rung5 与默认一致；批准执行）

- 用户释放 Chrome 占用后，桥重载为 `ddk_feature_set=2`，rung5 全 0；
  再换默认重载同链：89=89 桥调用逐项一致。DDK2 可达性未确认（未跑 r78 的非零 CCB create）。
- 模块现为**默认参数的新桥**（已重载，build 与旧 freeze 不同）；引用 0/113；无新 WARN。
- 证据：`mt-vgpu-guest/reports/r132-ddk-switch-live-rung5.md`。
- 遗留：r78 非零 CCB create 命令需重建；69 提交未 push。

---
