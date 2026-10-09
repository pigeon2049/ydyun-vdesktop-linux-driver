# r440：Tile 打包活体仍超时——+0x50/+0x58 嫌疑被证伪

**结论：Header `+0x50`/`+0x58` 填 tile 打包值后固件仍 5s 超时（-ETIMEDOUT）。r438/r439 的"0 tiles 致挂起"假说被活体证伪。**

## 活体过程（最高风险，全部按安全协议执行）

1. **系统确认**：第 7 次冷重启后 uptime 20min，`mt_guest_probe`/`mt_pvr_bridge` 均未加载，工作区干净。
2. **Pre-live 门禁**：`check-offline` **557 Python + 783 C 全绿**（1 skipped，1 pre-existing ResourceWarning）。
3. **双门控测试构建**：`MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`（static_assert 临时中和；userspace `n_entries` 1→0 临时；事后全部 revert）；`make kernel` W=1 **零警告**。
4. **Trial 重建**：probe 全参数链 + `runtime_context=1`：`firmware trial: connect=0 disconnect=-61 restored=0 pinned=1 result=0`（r436 教训已应用）。
5. **桥加载**：双门控 `mt_pvr_bridge.ko` insmod，`/dev/dri/renderD128` 就绪。
6. **mt-ta-readback 全链路**：connect（bvnc=0x23000406600017）→ render ctx handle=0x1000 → 11 BOs + 12th target BO（va=0x7b000000）+ 13th RgnHeader BO（va=0x7c000000 bytes=4096，逐 dword 填 1，dmesg 确认 `r431: rgnheader BO bound`）→ 0xFD 提交（Header：`+0x10`=0x7c000000 RgnHeader VA，`+0x50`/`+0x58`=`0x0001000000000000` tile 打包 64×64，`n_entries`=0）→ fence 分配 → **5s 超时**（errno=110）。

## 关键对比表

| 轮次 | +0x10 | +0x50/+0x58 | RgnHeader 填充 | 结果 |
|---|---|---|---|---|
| r414 | 0（全零） | 0 | — | OK 219µs（无工作快路径） |
| r425 | 像素 BO | 0 | — | 超时 |
| r432 | RgnHeader | 0 | 0xFF/dword | 超时 |
| r436 | RgnHeader | 0 | 0x01/dword | 超时 |
| r440 | RgnHeader | **tile 打包** | 0x01/dword | **超时** |

## 分析

r438 的"0 tiles 或致固件挂起"为本轮最强嫌疑，但活体证伪：tile 打包值（64×64→`0x0001000000000000`，[MEASURED] FUN_00184220）未能使固件完成。

当前 Header 状态（vs UMD 单 RT 必写清单 r438）：
- ✅ `+0x10` = RgnHeader VA（[MEASURED] r430 三跳链）
- ✅ `+0x50`/`+0x58` = tile 打包（[MEASURED] r438/r439）
- ✅ `+0x68` = 0（单 RT 下 UMD 写布尔；我方 w/h=64 恒 0，[MEASURED] r438）
- ✅ `+0x28`/`+0x30` = 0（UMD 单 RT 恒 0，[MEASURED] r437）
- ❓ `+0x120`（flags 位打包）、`+0x138`–`+0x160`（feature 条件）仍为 0 —— **下轮嫌疑**
- ❓ RgnHeader per-dword 内容（全 1 为 UMD 行为 [MEASURED]，固件是否要求未知）

## Teardown

- pending TA fence → bridge ref=1，`scripts/safe_rmmod.sh` **正确拒绝**（未用 `-f`，遵守红线）。
- dmesg 零 WARN/BUG/Oops（仅 boot 期 CPU 通告）。
- 源码已 revert 为 committed 默认门控状态；默认重建 W=1 零警告；工作区干净。
- **待用户第 8 次冷重启清除**（同 r406/r418/r421/r425/r432/r436 前例）。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**557 Python + 783 C 全绿**
- `make kernel` W=1：**零警告**（双门控测试构建 + revert 后默认构建）

## 诚实边界

- tile 打包 [INFERRED]→仍未 [MEASURED]：本次为证伪性证据。
- T2 像素回读仍 open；生产代码零行为变更；单发测试。
- **下一步必须离线**：`+0x120` flags 位打包与 `+0x138`–`+0x160` feature 条件字段。**不再做无离线依据的活体试探**（r380/r418/r421/r425/r432/r436/r440 教训）。
- 本轮零 `rmmod -f`、零自行重启；未链入下一轮。
