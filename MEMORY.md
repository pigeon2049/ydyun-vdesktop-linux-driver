# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-04（r134 门控=DRM major，订正 r131；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r134：DDK2 门控=DRM version_major==2，离线；零硬件触碰）

- 语料（SHA 已对）：UMD 自 calloc 特性块，`+0x54=(drm major==2)+1`；桥 `.major=0`→1→legacy。
  r131 开关写的是 UMD 不读的内核块，故 r132/r133 无差异（事实仍成立，作用点错）。
- 下一步：桥加 `drm_major` 参数（默认 0），重载 `=2` 跑 r133 同链（需批准）。
- 无需重启：模块干净卸载重载，引用 0/113，无 D 态。证据：`reports/r134-ddk-gate-is-drm-major.md`。
- 遗留：71 提交未 push；`ddk_feature_set` 去留待定。

---

## 本次会话进展（r133：非零 CCB create + ddk_feature_set=2，仍无差异；批准执行）

- 按 r76/r77 还原 r78 命令（pack 0x0733），仅 create+destroy 不 kick；
  `=2` 与默认各重载各跑，桥调用 91=91 逐项一致，无 SyncPrim/SubmissionBuf。
- 模块停在默认参数新桥，引用 0/113，无新 WARN。
- 下一步（离线）：语料核 `RGXCreateKickSyncContextCCB@0x52180` 门控读取点，别再盲重载。
- 证据：`mt-vgpu-guest/reports/r133-ddk2-ccb-create-live.md`。遗留：70 提交未 push。

---
