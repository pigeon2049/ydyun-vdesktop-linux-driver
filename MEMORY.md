# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r88 TDM 桥备好未加载；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r88：0x89 TDM 桥备好，未加载；自主推进）

- 按 r87 最小实现：`0x89:0x5`（双别名同一 8K PMR）+ `0x89:0x6`
  （正常释放，二次诚实 ENOENT）；H1 假设（同 PMR 可满足）待活体验收。
- 门禁：新测试 5 项 + 反向验证通过；L1 226+268 全绿；kernel exit 0
  无新增警告；wire_sizes 以 None 接入（生成器产物未动）。
  代码提交 `95a7492`（纯加法 196 行，与文档分开）。
- 红线：新桥**未加载**（活会话仍 `894faf50`）；加载 + L3/L4 + 重放
  打包为"加载窗口"，需明确批准。零硬件触碰。
- 证据：`mt-vgpu-guest/reports/r88-tdm-bridges-ready.md`。
- 遗留：加载窗口；T3 继续；push 待批。

## 本次会话进展（r87：Rogue2D fabricated spike；离线自主）

- 单个 `R2DCreateContext` 走 95 条桥调用后干净 unwind（返 3）；
  卡点 `0x89:0x5 GetSharedMemory`（OUT 20：eError + 2 指针，fabricated 零填充）；
  我方桥 0x89 组全空（`-ENOTTY`）。0x89 TDM 全表 11 项已列；
  最小实现评估：复用 pmr_new + mmap，比 DDK2 门小。
- 零硬件触碰（fabricated 结论自足，未跑活体）。
  证据：`mt-vgpu-guest/reports/r87-rogue2d-spike.md` + jsonl。
- 遗留：0x89:0x5/0x6 已实现待加载（见上节）；T3 继续；push 待批。
