# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r78 活体 legacy 定案；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r78：活体非零 CCB create 仍走 legacy；批准的单次实验）

- 用户批准真机：passthrough 非零 CCB create（pack `0x0733` 活体生效），
  其后 0 SyncPrim 调用 → legacy 分支；DDK2 同址崩（dmesg `at 48` 写 fault 吻合）。
  根因：桥 `mt_pvr_device.h:143-145` 故意钉 `features+0x54<2`（bring-up 刻意选择）。
- 决策：update/DDK2 从"缺输入"转为"需桥特性开关"（改代码+重编+重载，
  单独立项单独批准）；check 侧即翻译器当前完整输入；T3 仍被 CCB 卡住。
- 会后零残留（23/34/38/0 全对，D 态 0，无新增 WARN）。
  证据：`mt-vgpu-guest/reports/r78-live-ccb-legacy-path.md` + jsonl。
- 遗留：特性开关立项（待批）；真实 CCB 内容仍需绘制路径。

## 本次会话进展（r77：DDK2 rsi 身份落定；离线语料）

- §9 流程首验：DDK2 rsi = 走完完整创建的 kicksync 对象；
  `+0x8/+0x18/+0x28` = 注册 server ctx / `_SyncPrimAlloc` /
  `SubmissionBufAlloctorCreate`（语料行号 L28320/28321/28331）；
  render-obj 候选彻底排除；`param_4` 是可选 OUT，传 0 正确。
- 推论：DDK2 唯一可达路径 = 活体非零 CCB create 走完三步门控
  （待批实验已精确到"对象已知、只差一次活体 create"）。
- 零硬件触碰，无新 trace。证据：`mt-vgpu-guest/reports/r77-ddk2-rsi-identity.md`。
- 遗留：live 非零 CCB create + DDK2（待批）；真实 CCB 内容仍需绘制路径。
