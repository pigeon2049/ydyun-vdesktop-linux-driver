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

最后更新：2026-10-06（r183 ref 离线审计；首要嫌疑已命名；未 push）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r183：ref 漂移离线审计）

- 零硬件触碰（只读代码审计 + `lsmod` 只读；未重载模块、未提交 GPU 工作；`dmesg` 本容器无权读）。首要嫌疑已命名：`pvr_file_release` 双 early-return（unbind/destroy 失败即 `return`，`mt_pvr_bridge.c:868-875`）可 abandon 整文件 PMR 的 `dma_owner`（每 map +1），量级 ≈14/轮与失败轮 +14 同形；活体 probe Used by=66（=1+65）只读吻合。prepare 失败路/DMA 注册释放经走查配平，已排除。以上为推断，活体差分待可重载窗口（需批准）；释放语义未动。
- 证据：`reports/r183-ref-audit-offline.md`。候选下一步：活体差分（defaults legacy 差分，需批准）→ 真发射。

---

## 本次会话进展（r182：TQX bring-up 打通）

- 真机已批准（含内核改动+多次 `=2` 重载，均 ref0 操作，probe 会话零触碰语义）。初版 `-22` 经分步日志定位为 TQX 块在 process 创建之前（`!p->store`）；拆分为 Bo 绑定（seal 前）+ flavor-1 创建（process 后 DM 上下文旁）后，活体报 `tqx-ctx: ready`，bring-up 打通（无 GPU 动作）。
- ref 记账：translator 持有集 +18 随 rmmod -18 对称归零（对称性实证）；失败 prepare 轮次另累计 +65（≈14/轮 ≈ map 数，未解释；无残留/fd，功能完好，仅禁 unload——unload 本就被 freeze 禁止）。代码 param 门控已入库（282+292，反向全过）。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r182-tqx-bringup.md`。候选下一步：ref 审计（defaults legacy 差分）+ 真发射。

---

