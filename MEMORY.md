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

最后更新：2026-10-07（r204 KickTA3D5 ABI 边界）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r204：KickTA3D5 ABI 边界）

- 零硬件触碰。r203 fabricated trace 确认 `0x82:0x14` 为 108/4；SHA 对版 UMD wrapper 传入长度 108。2.7.1 生成结构编译为 96/4，2.3 Guest 无此结构，当前 dispatcher 缺 handler。5.2 Host schema 审计大小匹配但不能证明 Guest 支持。
- 结论：暂不把 2.7.1 结构直接用于该 UMD 请求，先恢复 108 字节逐字段契约并确认目标 Guest handler 语义。
- 证据：`reports/r204-kickta3d5-abi-boundary.md`，r203 trace，`reports/legacy-umd-pvr-bridge-abi.json`。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r203：GFX update fabricated 干净返回）

- 零硬件触碰。GDB watchpoint 证明 `RGXKickGfx` 在 RVA `0x7ee1a` 将 0x408 字节复制到仅 0x80 bytes 的 b24，覆盖 update-list chunk size（`0x91→0x1151`），造成 r201 free abort。b24/b25 扩为 0x410 后 header 完整，`0x82:0x14` 发出且 RGXKickGfx 返回 0、进程正常退出。
- 证据：`reports/r203-gfx-update-clean.md` + trace。下一步离线核对并补齐 bridge `0x82:0x14` handler ABI；真实 CCB 仍冻结待批准。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---
