# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-07（r182 bring-up 打通；ref 部分澄清）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r182：TQX bring-up 打通）

- 真机已批准（含内核改动+多次 `=2` 重载，均 ref0 操作，probe 会话零触碰语义）。初版 `-22` 经分步日志定位为 TQX 块在 process 创建之前（`!p->store`）；拆分为 Bo 绑定（seal 前）+ flavor-1 创建（process 后 DM 上下文旁）后，活体报 `tqx-ctx: ready`，bring-up 打通（无 GPU 动作）。
- ref 记账：translator 持有集 +18 随 rmmod -18 对称归零（对称性实证）；失败 prepare 轮次另累计 +65（≈14/轮 ≈ map 数，未解释；无残留/fd，功能完好，仅禁 unload——unload 本就被 freeze 禁止）。代码 param 门控已入库（282+292，反向全过）。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r182-tqx-bringup.md`。候选下一步：ref 审计（defaults legacy 差分）+ 真发射。

---

## 本次会话进展（r181：transfer dry-run 活体验证）

- 真机已批准（含内核改动+三次 `=2` 重载，均 ref0，probe 零触碰）。`translate_transfer` dry-run：首轮选择 bug（CCB PMR 胜出→错配大声失败）修复后，次轮程序 digest 与离线预言逐位一致（`0xd893...`）；附带修 fill 未初始化（双门禁）。278+292 全绿，反向全过。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r181-dryrun-verified.md`。候选下一步：真发射（scratch+fill+fence+回读）。

---

