# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r92 判别式 verdict；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r92：判别式 verdict；离线自主）

- OUT 逐字节一致 → 输入侧问题；嫌疑 `AcquireCPUMapping` 空连接
 （`lVar2+0x10` 无人写）；证伪命令已备好（gdb 读槽 / 读完 8aaa0）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r92-discriminant-verdict.md`。
- 遗留：+0x10 证伪；加载窗口；push 待批。

## 本次会话进展（r91：import 拒绝点收敛；离线自主）

- r90 卡点收敛到桥后三者（979b0/97630/AcquireCPUMapping）；
  排除 eError 位置（厂商头居尾实锤）；判别式备好
  （r2d2 vs rung8 的 0x6:0x6 OUT 逐字节比对定论）。
- 副产品：MapMem 描述符表格式（count@+4，步长 0x4c）；
  MapUSCMem 多 MapToDevice（已实现）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r91-import-rejection.md`。
- 遗留：OUT 判别式执行；加载窗口；push 待批。
