# r435: 第 6 次冷重启未发生（bridge ref=1），停止活体

> **结论：停止。未执行任何活体操作。** 零硬件触碰（除只读检查外）。

## 停止依据（只读检查，[MEASURED]）

| 检查项 | 结果 |
|---|---|
| 启动时间 | ~2026-10-09 16:20:46 CST（由 dmesg -T 反推：17:43:24 − 4958s） |
| r432 活体时刻 | 2026-10-09 17:45:43（dmesg：render context READY，13th rgnheader BO bound） |
| 第 6 次冷重启 | **未发生**——启动（16:20）在 r432 活体（17:45）之前，之后无重启 |
| `mt_pvr_bridge` ref | **1**（`lsmod`）——r432 pending fence 遗留（r432 报告已记录 safe_rmmod 拒绝） |
| `mt_guest_probe` ref | 1（正常：bridge 依赖） |
| 残留会话 | 完整 render context（11 BOs + 12th target va=0x7b000000 + 13th rgnheader va=0x7c000000，CSW built，exec created，READY），17:45 后无 submit/teardown |

任务停止条件命中：`若未冷重启（bridge 仍 ref=1），立即停止并报告，不进行活体。`

## 未执行项

- 未构建双门控测试模块；未重载 bridge；未运行 `mt-ta-readback`；
- 未触碰残留会话（遵守：一次一个 live 实验模块；不得 unbind 冻结会话）。

## 门禁（离线）

- `make -C mt-vgpu-guest check-offline`：**554 Python + 1491 C 全绿**（1 ResourceWarning，pre-existing）
- `make kernel` W=1：零警告（默认门控构建，无需重编）

## 证据

- `mt-vgpu-guest/build/traces/r435/dmesg-r435.txt`（0600）

## 下一步

r435 活体（RgnHeader 逐 dword 填 1）的前提是用户执行第 6 次冷重启。
冷重启后需重新验证：uptime、`lsmod`（bridge 未加载或 ref=0）、dmesg 无残留会话，
方可进入 r436（或重开 r435）活体。

## 诚实边界

- 启动时间由 dmesg -T 反推，±2s；未做跨源交叉验证。
- 残留会话归属判定为 r432（dmesg 有 `r431: rgnheader BO` + `r432` 报告的 ref=1 记录），[INFERRED] 但高置信。
- 本轮零硬件触碰；生产代码零变更。
