# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r75 DDK2 全映射 + 崩溃归因；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r75：DDK2 结构体全映射 + rsi 需求定位；离线）

- DDK2 第 3 参数全映射：update（`0x0` 条数 + `@0x8+i*16` u64/u64 条目）
  + check（`0xd8` 条数 + `@0xe0+i*16`）+ 转运指针（`0xc8/0xd0`）+
  server 统一数组（check slot0–11，分隔 12，update 13 起）。
- `+1490` 崩溃精确归因：`rdx=[rsi+0x28]=NULL`（自我修正 r74 的误读）；
  rsi 须是富对象，首位候选 render 客户端对象（`0x330`）。
  `RGXCreateKickSyncContext` 已证伪（即 CCB 包装）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r75-ddk2-struct-map.md`
  （本轮崩溃 trace 无桥流量，未归档 jsonl）。
- 遗留：render-obj 喂 DDK2 rsi 验证；真实 CCB 内容仍需绘制路径。

## 本次会话进展（r74：update 侧不在 RGXKickSync 路径上；离线证伪）

- `0x1b0=1`、`a3` 结构体各 4/4 fabricated 验证：`update` 恒为 0；
  静态穷举确认 b26 只有一个条数（`0xd8`）——update 数组输入在本路径无来源。
- 新候选 `RGXKickSyncDDK2`：同构循环但 `{u64,u64}` 条目 + `0x20` 步长；
  rung8 形状参数在其 `+1490`（`mov %rax,0x48(%rdx)`，rdx=NULL）6/6 定崩，
  需解完整入参（下一轮），勿在 b26/a3 上继续穷举。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r74-update-side-ddk2.md` +
  `r74-update-side-negative.jsonl`。
- 遗留：DDK2 入参 shaping；真实 CCB 内容仍需绘制路径；快照刷新 pass 待攒。
