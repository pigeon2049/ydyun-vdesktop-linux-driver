# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r76 ghidra 语料 + SubmissionBuf 定位；最旧节已归档）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（r76：ghidra 语料指路 DDK2；离线）

- 用户指路 `/opt/MTT-driver-only/` + `ghidra-projects/`：语料
  `decompiled/linux-legacy-umd-5.2.0/`（SHA 与在用 UMD 一致）成首选 RE 路径，
  伪 C 纠正 r74/r75 两处误读；以后 RE 结论先过语料。
- DDK2 `+0x28` = `SubmissionBufAlloctorCreate` 在 create 期填入
  （门控：features>=2 + SyncPrim 两步；`+0x48` 出生置零即崩溃点形状）。
  r75 的 render-obj 候选被取代（旧报告留档不改）。
- CCB pack 公式 trace 实测：`ui32PackedCCBSizeU88=(arg5&0xff)<<8|(arg4&0xff)`。
- 边界：fabricated 恒走 legacy 分支，DDK2 离线不可驱动；
  下一步 = 活体 passthrough 非零 CCB create（需单独批准）。
- 证据：`mt-vgpu-guest/reports/r76-ghidra-ddk2-submissionbuf.md` +
  `r76-ccb-size-pack.jsonl`。
- 遗留：live 非零 CCB create + DDK2（待批）；真实 CCB 内容仍需绘制路径。

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
