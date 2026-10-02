# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r105 存储点；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r105：+0x54 存储点定位；离线自主）

- 唯一写入 bb13（bsr 对齐数学）；跳过 bb17（edx==0）是我方路径；
  Layout 不支持裸调（负偏移读调用者栈），转 R2DCreateSurface 入口。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r105-plus54-store.md`。
- 遗留：Surface 入口签名；加载窗口；push 待批。

## 本次会话进展（r104：memsize 计算链定位；离线自主）

- sc=4 消采样门；新门 memsize-zero，计算链 imul×2→[r15+0xd0]；
  宽/高/format/采样三维排除，剩 validator 返回 + 对象字段。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r104-memsize-chain.md`。
- 遗留：乘数溯源；加载窗口；push 待批。
