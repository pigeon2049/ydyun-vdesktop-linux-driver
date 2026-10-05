# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-05（r160 CCB 窗口字段对照；r159 见正文）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r160：SubmitTransfer3 CCB 窗口字段对照）

- 零硬件触碰。shim `ccb_resolve` 加 `runs`（非零 runs 上限 32，合成门禁断言首 run）；单轮 blit 重放（major 2 + shared backing）27 runs 恰好覆盖窗内 39B。`+0x10`=CCB+`0x58`、`+0x28`=`0x1078` 与语料 `SubmissionCmdGenerate`（SHA 已核）的 `0x58`/`0x1020` 定长拷贝形状吻合；`+0x40` 的 2B 三轮各异（余 37B 一致），来源未定，候选 ASLR/未初始化。
- `FUN_0015f890`（`TQSubmissionSubmit`）确认提交链：check（`flag&1`）/update（`flag&2`，与 r159 一致）编组后进 `BridgeRGXTDMSubmitTransferDDK2`；本轮 `update_count=2` 与之相符，update 数组走 IN 指针、不在 CCB 窗口内。
- 门禁全绿：270 Python（1 skip）+272 C，shim `-Werror`，`lsmod` 无 `mt_*`。证据：`reports/r160-ccb-window-fields.md` + trace。下一步离线 GDB 对 `+0x40` 下写观察点；换 producer 闭合扩展区条目算术。

---

## 本次会话进展（r159：update 语义 UMD 侧离线确定）

- 零硬件触碰。`SyncUtilGenerateUpdateData` 确定三要素：布局（IN 36/44/52/60，与头一致）、可见性（`flag&2` 条目经 sync block 句柄+相对偏移，与桥 PMR/SYNC 跟随模型一致）、完成条件（poll 等 fd，先写回后交 fd）。`flag&2` 来源待活体（红线禁重载）。
- 顺手清 r148 调试残留（桥 5 处 DBG）；`make kernel` W=1 零警告，`make check-offline` 全绿。证据：`reports/r159-update-semantics-offline.md`。
- 遗留：工作区有 59 个未提交改动/未入库文件（含本轮，另有历史包袱）；`r135` jsonl 未动。
