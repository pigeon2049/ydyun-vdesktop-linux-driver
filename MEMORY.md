# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)（只读）。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-03（r72 fabricated 非零 kick 复现；按规则归档最旧节）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

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

## 本次会话进展（2026-10-03 文档复核与收敛）

- 复核：12 个引用路径全存在；在载 bridge build-id `894faf50`… 与快照一致；
  L1 重跑 221 Python + 268 C 全绿；活会话零变化
  （Guest/FW 2/2，pending=0/completed=23，引用 38/0）。
- 收敛两处：① 五份入口的裁决口径互相矛盾 → 统一为 STATUS → 快照 → MEMORY；
  ② 快照哈希的 amend 死循环 → 内容基线口径（只在改动快照内容时推进）。
- §7 容量表 10-03 重测（build 5.9G、reports 442 文件、tests 52C+31py、
  recovery 171 文件/54 源码）；ANALYSIS 陈年相对路径订正；
  PROTOCOL/FIRMWARE 抽 stub 入日期归档；AGENTS 检查单 +USB 短页同步项。
- “8 个符号”计数与快照 12 行阶梯表口径不一致 → STATUS 与目录入口改称“全链路”，不再计数。
