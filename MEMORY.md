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

最后更新：2026-10-07（r205 KickTA3D5 字段偏移）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r205：KickTA3D5 字段偏移）

- 零硬件触碰。SHA 匹配 UMD wrapper 栈布局与 r203 seq 123 fabricated 请求互证数组指针及三个 count 的偏移和值：check=1、update=1、sync PMR=0。
- 尾部 `0x48`–`0x5f` 有未定语义参数槽；与 2.7.1 `0x48` 起 VA/size/count 的布局不同，12 字节差异属于结构偏移问题，handler 仍不可安全补齐。
- 证据：`reports/r205-kickta3d5-field-offsets.md` + r203 trace。下一步查找 5.2 生成头或目标 Guest handler 对尾部字段的定义/读取路径。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r204：KickTA3D5 ABI 边界）

- 零硬件触碰。r203 fabricated trace 确认 `0x82:0x14` 为 108/4；SHA 对版 UMD wrapper 传入长度 108。2.7.1 生成结构编译为 96/4，2.3 Guest 无此结构，当前 dispatcher 缺 handler。5.2 Host schema 审计大小匹配但不能证明 Guest 支持。
- 结论：暂不把 2.7.1 结构直接用于该 UMD 请求，先恢复 108 字节逐字段契约并确认目标 Guest handler 语义。
- 证据：`reports/r204-kickta3d5-abi-boundary.md`，r203 trace，`reports/legacy-umd-pvr-bridge-abi.json`。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---
