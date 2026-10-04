# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-04（r131 ddk_feature_set 开关离线实现；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r131：DDK2 特性开关，离线实现，零硬件触碰）

- 桥加 `ddk_feature_set` 模块参数（默认 0=legacy），helper + 4 条断言；
  check-offline 全绿（C 272），`make kernel` W=1 无警告，反向验证已做。
- 未加载新 `.ko`；活会话 freeze 不变。启用需卸桥重载，待用户批准。
- 证据：`mt-vgpu-guest/reports/r131-ddk-feature-switch.md`。
- 遗留：68 提交未 push；STATUS 门禁行写 234+268，实为 234+272（刷新 pass 时改）。

---

## 本次会话进展（r130：契约裁决，测试过期；批准执行）

- 离线裁决：驱动 `if (r->out_syncobj)` 自 r40，强制要求从未存在；
  测试期望是 day-one 误期。改测试（bad[3]→正向断言）+ uapi 注释。
- 活体 smoke 全绿：13 非法拒、0-syncobj fill、16×16 像素三重验证、
  copy 闭环；3/3/0，模块已卸（WARN 累计 8，同签名）。
- probe 引用 87→113（+26）；桥探针全绿，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r130-fill-contract-verdict.md`。
- 遗留：67 提交未 push。

---
