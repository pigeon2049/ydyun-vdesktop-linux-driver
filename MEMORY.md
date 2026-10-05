# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-05（r161 +0x40 写入者落定；对照表见 r160）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r161：CCB +0x40 轮变 2B 写入者落定）

- 零硬件触碰，无代码改动。离线 GDB 对 PMR `0x500e` backing 窗口 `+0x40` 下硬件写观察点：唯一命中为 `SubmissionCmdGenerate` 的 `0x58B` 头拷贝（libc AVX `vmovdqu64`），调用栈 `TQJobSubmit → SubmissionCmdGenerate → PVRSRVMemCopy`（动态符号，非地址推算），活体确认 r156 路径。
- job（`$rsi`，堆地址跨轮稳定）`+0x40` 四轮 `0x288e→0x28ae→0x28bd→0x28d7` 单调递增、步长不等：计数器形态，排除 ASLR 指针；确切命名未定。证据：`reports/r161-plus40-writer.md` + `r161-plus40-watch.txt`。门禁复核全绿（269+272）。下一步换 producer 闭合扩展区条目算术。

---

## 本次会话进展（r160：SubmitTransfer3 CCB 窗口字段对照）

- 零硬件触碰。shim `ccb_resolve` 加 `runs`（非零 runs 上限 32，合成门禁断言首 run）；单轮 blit 重放（major 2 + shared backing）27 runs 恰好覆盖窗内 39B。`+0x10`=CCB+`0x58`、`+0x28`=`0x1078` 与语料 `SubmissionCmdGenerate`（SHA 已核）的 `0x58`/`0x1020` 定长拷贝形状吻合；`+0x40` 的 2B 三轮各异（余 37B 一致），来源未定，候选 ASLR/未初始化。
- `FUN_0015f890`（`TQSubmissionSubmit`）确认提交链：check（`flag&1`）/update（`flag&2`，与 r159 一致）编组后进 `BridgeRGXTDMSubmitTransferDDK2`；本轮 `update_count=2` 与之相符，update 数组走 IN 指针、不在 CCB 窗口内。
- 门禁全绿：270 Python（1 skip）+272 C，shim `-Werror`，`lsmod` 无 `mt_*`。证据：`reports/r160-ccb-window-fields.md` + trace。下一步离线 GDB 对 `+0x40` 下写观察点；换 producer 闭合扩展区条目算术。
