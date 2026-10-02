# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r74 update 侧证伪 + DDK2 定位；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r74：update 侧不在 RGXKickSync 路径上；离线证伪）

- `0x1b0=1`、`a3` 结构体各 4/4 fabricated 验证：`update` 恒为 0；
  静态穷举确认 b26 只有一个条数（`0xd8`）——update 数组输入在本路径无来源。
- 新候选 `RGXKickSyncDDK2`：同构循环但 `{u64,u64}` 条目 + `0x20` 步长；
  rung8 形状参数在其 `+1490`（`mov %rax,0x48(%rdx)`，rdx=NULL）6/6 定崩，
  需解完整入参（下一轮），勿在 b26/a3 上继续穷举。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r74-update-side-ddk2.md` +
  `r74-update-side-negative.jsonl`。
- 遗留：DDK2 入参 shaping；真实 CCB 内容仍需绘制路径；快照刷新 pass 待攒。

## 本次会话进展（r73：非零 check kick 上真机；单次 live 实验）

- 用户本轮明确批准真机测试：passthrough rung8 + 512B 手工结构体，
  一次即成，活桥 `0x88:0x4 ioctl_real ret=0 check=1 update=0`；
  dmesg `ufo_known=1/1`（真实 bridge sync PMR 句柄命中）——T2 活体验证。
  证据：`mt-vgpu-guest/reports/r73-live-nonzero-kick.md` + `r73-live-nonzero-kick.jsonl`。
- 无 GPU 执行，会后状态一字不差（pending=0/completed=23，引用 38/0，
  objects=34，D 态 0，无新增 WARN/BUG/Oops）；`make umd/probe` 仍禁用
  （首步 rmmod），只手跑 harness、无 timeout 包裹。
- 遗留：update 侧数组偏移定位；真实 CCB 内容仍需绘制路径；
  快照刷新 pass 待攒。
