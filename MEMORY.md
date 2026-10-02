# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r84 SubmitTA 回填；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r84：SubmitTA 回填映射；离线）

- T3 第三锹：两处桥调用 48 参数逐项回填；同步组装心脏
  （Query + tag 2/3 + builder + fence/update 双生成器）点名；
  重试语义（0x19→wait，我方 -ENOTTY 直接 break）澄清。
- fence/update 对偶与 check-only 首帧假设一致，无矛盾。
- 下步是执行验证二选一：A fabricated 同步整形 / B 活体 KickTA（待批）。
  零硬件触碰。证据：`mt-vgpu-guest/reports/r84-submitta-backfill.md`。
- 遗留：T3 执行验证；特性开关 + push 待批。

## 本次会话进展（r83：TA 提交链到桥 0x82:0xc；离线）

- 用户令快速推进：T3 第二锹。链 RGXKickTA→PrepareTA→SubmitTA→
  BridgeRGXKickTA3D2（0x82:0xc，268/12B，48 参数全字段已列）；
  fabricated 整形 6 迭代：门卫→+0x30 指针→+0x120 空写→
  uint 换算陷阱（byte 728 非 182）→越过（新 RIP）。
- `0x82:0xc` 尚未 fabricated 发出；下步 SubmitTA 回填映射 + 同步槽。
  零硬件触碰。证据：`mt-vgpu-guest/reports/r83-ta-submit-chain.md`。
- 遗留：T3 继续；特性开关 + push 待批。
