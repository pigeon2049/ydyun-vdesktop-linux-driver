# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r81 mock 排查 + DMA 活体验收；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r81：mock 路径排查 + DMA 活体验收；批准的真机测试）

- 用户要"真机测试 + 还有哪些走 mock"：dispatch 全分支定级——
  真干活（PMR/arena/DMA/台账/inspect）、成功空桩（stub×10 + OOM/ZS/compute，
  其中 EventObjectWait 最危险）、句柄作坊（ZS 13 参数全丢）、
  静态真形（connect/堆表/info，故意）、明确拒绝（S4 边界）。
- 活体 `pvr_dma_smoke` PASS（refs 38→39→38，plan + arena 行 + DMA IOVA/GPU PA 行）；
  会后零残留。无 rmmod/timeout/GPU 工作。
- 给翻译器的红线：wait 语义自己实现；ZS/compute/render 句柄无含义；
  mock 清单冻结为基线。证据：`mt-vgpu-guest/reports/r81-mock-audit.md` +
  `r81-live-dma-proof.txt`。
- 遗留：T3 recon；特性开关 + push 待批。

## 本次会话进展（r80：全景测试评估；用户指令）

- 用户要"分配→复制→渲染→执行"逐段测评：L1 本轮实跑全绿（221+268）；
  `make kernel` exit 0；dmesg 0 WARN；活体 sysfs 与 §12 一致；零硬件触碰。
- 新发现：干净重编有 4 处 r38 期旧警告（快照"零警告"须加"增量"限定，
  或另起小轮清掉）。
- 结论：分配/复制/合成渲染/手工执行全绿；缺口 = T3（主）、CCB 内容、
  特性开关、ZSBuffer、check-only 首帧假设、会话窗口。
- 证据：`mt-vgpu-guest/reports/r80-panorama.md`。
- 遗留：T3 recon 优先；旧警告清理小轮；特性开关 + push 待批。
