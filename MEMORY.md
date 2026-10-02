# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r114 输入侧闭环；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r114：fence 生成器语义；离线自主）

- `SyncUtilGenerateFenceData`：同步表→{handle,offset,值}三元组 + 上限钳制；
  update 侧同构；T1/T2 与厂商侧逐字节对应，r113 输入侧无盲区。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r114-fence-generator.md`。
- 遗留：首帧实现/DM2 发射（待新会话）；加载窗口；push 待批。

## 本次会话进展（r113：check-only 首帧翻译设计；离线自主）

- r79 承诺收敛：真 check-kick → T1/T2 → 自实现等待 → 空 DM2 marker →
  真 fence（deferred，非即时）；update≠0 诚实拒绝；执行待新会话。
- 设计文档不写代码。证据：`mt-vgpu-guest/reports/r113-checkonly-first-frame.md`。
- 遗留：首帧实现（待新会话）；加载窗口；push 待批。
