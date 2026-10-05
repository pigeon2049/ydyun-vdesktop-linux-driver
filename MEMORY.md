# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](MEMORY-HISTORY-2026-10-05.md)。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。

最后更新：2026-10-05（r158 Submit3 CCB VA→PMR 已关联并转储；归属成立）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）


## 本次会话进展（r158：SubmitTransfer3 CCB VA→PMR 关联与转储）

- 零硬件触碰。shim 新增 VA 台账（`0x6:0x15` reservation 范围 + `0x6:0x13` pmr↔reservation），`0x89:0xa` 提交前按 r151 ABI（ccb@88/bytes@104）解出 VA 并定位 backing，记 `ccb_resolve`（窗内非零数/首非零/FNV-1a/32B 采样；不可达记 `resolved:0`）。
- 重放 `musa_blit_test -device 0 -f -o`（major 2 + shared backing，`timeout -s KILL 15` 终止，UMD 不退出与 r157 相同）：Submit3 解码 `check=0/update=2/pmr_sync=0/ccb=0x8000f44000/0x1200`；`ccb_resolve` 给出 reservation `0x900d`（`0x8000f430fe`+`0xa00fff`）→ PMR `0x500e` → backing `0x500e000`+`0xf02`，窗内 39 非零、首非零 `+0x10`、FNV `0xb9e0f1a18201bf0f`；整块 10MB backing 非零同样 39、首非零 `0xf12`（=`0xf02+0x10`）、采样相同，归属成立。shim 回包仍 fabricated，不证明执行。
- 门禁全绿：269 Python（1 skip，含新增 `test_pvr_shim_ccb_resolve`：合成 reserve→map→mmap→写 pattern→Submit 断言 `resolved/backing_offset/nonzero` 与越界 `resolved:0`，反向关 shared backing 无记录）+272 C，shim `-Werror` 通过。`lsmod` 无 `mt_*`。下一步对照 `SubmissionCmdGenerate` 语料解读 39B 稀疏窗口。

---

## 本次会话进展（r157：fabricated DDK2 SubmitTransfer3 producer）

- 零硬件触碰。给 shim 增加 opt-in `UMD_DRM_MAJOR=2`，默认 major 1；`musa_blit_test -device 0 -f -o` 在 shared-backing 模式到达 `0x89:0xa`（108B/4B），trace 527 行、105 bridge ioctl、15 个 submit 前 PMR snapshots、无 passthrough。按 r151 ABI 解码出 CCB GPU VA `0x8000f44000`、长度 `0x1200`，fake ioctl 后终止离线进程，未将等待/完成伪装成执行成功。
- Snapshot 显示 `0x1003000/0x1004000` 与 TQCB `0x5006000`–`0x5008000` 为零；`0x500d000` 有 2,621,440 非零字节，但没有 GPU VA → PMR 证据。修复 snapshot registry 在 munmap 后遗留过期视图并与读取互斥；反向注入时测试进程 SIGSEGV，恢复后通过。`musa_tq_performance_test -n 1` 未到 Submit3，先 SIGABRT。
- 门禁全绿：268 Python（1 skip）+272 C，shim `-Werror` 编译通过；major override 的默认/opt-in/非法值测试通过，反向固定 major=1 可抓回归。下一步映射 CCB GPU VA 到 PMR backing，再验证对应 `0x1200` 字节。快照 §6 计数仍旧，留待刷新 pass；未动硬件。
