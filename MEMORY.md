# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r98 悬空 P；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r98：P 是悬空指针；离线自主）

- 硬件观察点双命中（bt 全在 calloc←CCB）：P 块已被释放又被复用；
  r96/r97"计数空"修正为"悬空读取"；很可能单根因（MapMem）级联。
- 下步：修好 MapMem 后看 P 自然转正；不用追 P 本身。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r98-dangling-p.md`。
- 遗留：MapMem 一口气；加载窗口；push 待批。

## 本次会话进展（r97：P 槽位直接观测；离线自主）

- 断 CCB 出口：P 有效（堆），`+0x50=0x3000/+0x54=0/+0x58=0xf`；
  问题收敛为"+0x54 写入者是谁"（缺省零 vs 未调用的前置步骤）。
- 下步：硬件观察点抓写入者。零硬件触碰。
  证据：`mt-vgpu-guest/reports/r97-p-slot-observed.md`。
- 遗留：+0x54 写入者；加载窗口；push 待批。
