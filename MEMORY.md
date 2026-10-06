# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-06（r186 预设值抽取；未 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r186：scene 预设值抽取）

- 零硬件触碰。bridge 与 6 live 文件重复写死的 scene VA 收敛到新建
 `kernel/mt_addr_plan.h`（15 宏）；stream 端点等异语义字面量保留。
 过程撞车 `MT_TQX_STATE_BYTES`（`mt_tqx_copy.h` 同名 `0xa8`）被 `W=1`
 抓获，改名解决——此前碰撞检查被 `head -3` 截断，教训重演。
 新增门禁 5 项 + 反向验证；全量 287+292 全绿，`W=1` 零警告。
- 证据：`reports/r186-addr-plan.md`。候选下一步：等硬件批准（关账 / 真发射 / `=2` update）。

---

## 本次会话进展（r185：快照刷新 pass）

- 零硬件触碰。r164 后积压 20 轮，快照 §1/§2/§5/§6/§11 已同步 r165–r184
 （门禁实测 282 + 292；pmr 门禁 11→58；§5 重写为 ref 收尾 → 真发射 → update → TA/CDM）。§12 历史计数为同期记录，不动。
- 证据：`reports/r185-snapshot-refresh.md`。候选下一步：等硬件批准（kill-while-busy 关账 / 真发射 / `=2` update 验证）。

---

