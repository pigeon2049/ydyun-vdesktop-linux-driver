# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r106 依赖澄清；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r106：依赖倒挂澄清；离线自主）

- Surface 系门全过但死于 Context 欠堆：唯一真卡点仍是 +0x54；
  停调 surface，转回跟 create-struct byte+8 装配源。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r106-dependency-order.md`。
- 遗留：byte+8 源；加载窗口；push 待批。

## 本次会话进展（r105：+0x54 存储点定位；离线自主）

- 唯一写入 bb13（bsr 对齐数学）；跳过 bb17（edx==0）是我方路径；
  Layout 不支持裸调（负偏移读调用者栈），转 R2DCreateSurface 入口。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r105-plus54-store.md`。
- 遗留：Surface 入口签名；加载窗口；push 待批。
