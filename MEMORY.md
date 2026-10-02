# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r83 TA 提交链到桥；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r83：TA 提交链到桥 0x82:0xc；离线）

- 用户令快速推进：T3 第二锹。链 RGXKickTA→PrepareTA→SubmitTA→
  BridgeRGXKickTA3D2（0x82:0xc，268/12B，48 参数全字段已列）；
  fabricated 整形 6 迭代：门卫→+0x30 指针→+0x120 空写→
  uint 换算陷阱（byte 728 非 182）→越过（新 RIP）。
- `0x82:0xc` 尚未 fabricated 发出；下步 SubmitTA 回填映射 + 同步槽。
  零硬件触碰。证据：`mt-vgpu-guest/reports/r83-ta-submit-chain.md`。
- 遗留：T3 继续；特性开关 + push 待批。

## 本次会话进展（r82：T3 recon 第一锹；离线）

- 用户令快速推进：T3 开工。mtkm64 语料无 kick 语义（host KMD），排除并记因；
  转向 UMD `PrepareTA@0x178800`（L52052）：TA 提交记录布局 + 控制字落点 +
  `features+0x54` 门控第三次出现（legacy 基址 `+0x38`，范围减半利好）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r82-t3-recon-redirect.md`。
- 遗留：T3 第二锹（KickTA 上下 + DM2 信封对照）；特性开关 + push 待批。
