# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r73 非零 kick 上真机；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

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

## 本次会话进展（r72：fabricated 非零 kick count 复现；零硬件触碰）

- STATUS 下一步 1 前半闭环：512B 手工结构体驱动 `RGXKickSync`，
  fabricated 重放 6/6 发出 `0x88:0x4 check=1 update=0`；
  gdb（`UMD_TRAP="136:4"`）验证数组内容即注入值（sync 句柄 `0x600c` + fence 值 1）。
  证据：`mt-vgpu-guest/reports/r72-kick-nonzero-fabricated.md` + `r72-kick-nonzero-check1.jsonl`。
- 结构体映射（反汇编实测）：u32 count @`0xd8`（上限 12 条展开）；
  条目 `{u64 @0xe0+i*0x10, u32 @0xe8+i*0x10}`；第二个条数 u32 @`0x1b0`
  （update 侧数组偏移未定位，下一步）；结构体下限 **436 字节**，
  rung8 的 224B 只是零值下恰好不炸（`0x1b0` 越界读落新鲜零页）。
- 附带：fabricated 重放约 1/4 概率在 `RGXCreateRenderContext` 段错误，
  重试即过（纯用户态堆垃圾敏感，无硬件影响；coredumpctl 有记录）。
- 活会话零变化（Guest 引用 38，bridge 0，`card1/renderD128` + `card2/renderD129`；
  快照 §12 无需改）；真机抓包仍冻结（对象存储满 + 需单独批准）。
- 遗留：update 侧数组偏移定位；门禁加"结构体下限 436B"断言（等 update 侧一起落）；
  快照 §§1–11 仍停 bA43、刷新 pass 待攒（见归档尾）。
