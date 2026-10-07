# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](MEMORY-HISTORY-2026-10-07.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-07（r206 KickGFX5 Host schema 映射）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r206：KickGFX5 Host schema 映射）

- 零硬件触碰。对版 5.2 DKMS 包 SHA=`e3f684b1…` 的生成头将 `0x82:0x14` 映射到 `MUSAKICKGFX5 +20`；schema 的 flags/VA/size/submissionID/count 偏移和值逐项匹配 r203 fabricated trace。
- 12B 偏移差现已由额外 flags 与 submissionID 字段解释；同包 `MTGPUMUSAGFX5KM` 声明给出 sync/PMR 数组与提交参数接口顺序，但没有 handler 实现体；下一步核对 Guest handle 台账和实际 CCB 执行路径。
- 证据：`reports/r206-kickgfx5-schema.md`、r203 trace、`downloads/mthreads-dkms_5.2.0_amd64.deb`。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r205：KickTA3D5 字段偏移）

- 零硬件触碰。SHA 匹配 UMD wrapper 栈布局与 r203 seq 123 fabricated 请求互证数组指针及三个 count 的偏移和值：check=1、update=1、sync PMR=0。
- 尾部 `0x48`–`0x5f` 有未定语义参数槽；与 2.7.1 `0x48` 起 VA/size/count 的布局不同，12 字节差异属于结构偏移问题，handler 仍不可安全补齐。
- 证据：`reports/r205-kickta3d5-field-offsets.md` + r203 trace。下一步查找 5.2 生成头或目标 Guest handler 对尾部字段的定义/读取路径。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

---
