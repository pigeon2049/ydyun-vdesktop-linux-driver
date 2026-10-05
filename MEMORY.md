# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-05（r162 producer 探针；CCB 表见 r160–r161）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r162：换 producer 探针）

- 零硬件触碰，无代码改动。`-n 2` fill 与基线机器比对：VA/长度/38B 全等，仅 `+0x40` 计数器不同——fill CCB 与源面数无关。copy（去 `-f`）在 major 1/2 下同点 SIGABRT（`0x500d000` 映射后，`TQJobSubmit` 内 copy-setup 分发深处，无信息；rsp 对齐序断帧链，深挖止损），该 producer 当前不可达。
- 证据：`reports/r162-producer-sweep.md` + `r162-n2-fill.jsonl` + `r162-copy-abort.jsonl`。门禁复核全绿（269+272）。结论：扩展区算术缺可复现的新 producer；CCB 侧收敛，候选转向步骤 2（待可重建会话）。

---

## 本次会话进展（r161：CCB +0x40 轮变 2B 写入者落定）

- 零硬件触碰，无代码改动。离线 GDB 对 PMR `0x500e` backing 窗口 `+0x40` 下硬件写观察点：唯一命中为 `SubmissionCmdGenerate` 的 `0x58B` 头拷贝（libc AVX `vmovdqu64`），调用栈 `TQJobSubmit → SubmissionCmdGenerate → PVRSRVMemCopy`（动态符号，非地址推算），活体确认 r156 路径。
- job（`$rsi`，堆地址跨轮稳定）`+0x40` 四轮 `0x288e→0x28ae→0x28bd→0x28d7` 单调递增、步长不等：计数器形态，排除 ASLR 指针；确切命名未定。证据：`reports/r161-plus40-writer.md` + `r161-plus40-watch.txt`。门禁复核全绿（269+272）。下一步换 producer 闭合扩展区条目算术。
