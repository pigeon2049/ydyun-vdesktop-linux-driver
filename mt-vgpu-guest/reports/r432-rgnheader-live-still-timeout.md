# r432: RgnHeader 活体——固件仍 5s 超时，RgnHeader 非充分条件

> Round r432 (2026-10-09). **最高风险轮。** r431 RgnHeader 实现后的首次活体验证。

## Conclusion

**RgnHeader BO 正常创建绑定（va=0x7c000000），TA Header +0x10 正确指向 RgnHeader，但固件 5s 内仍无完成事件（-ETIMEDOUT）。RgnHeader 是必要非充分条件；`+0x28`/`+0x30` 或 RgnHeader 内容语义仍有缺失。**

## Live procedure

1. **双门控测试构建**：`MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`
   （static_assert 临时中和为 ==1；userspace `n_entries` 1→0 临时；事后全部 revert）；
   `make kernel` W=1 **零警告**。
2. **Pre-live T1/T2/T3/T4/T5**：`check-offline` **554 Python + 1490 C 全绿**（1 skipped）。
3. **Trial 重建**：第 5 次冷重启后 probe 全参数链加载，
   dmesg：`firmware trial: connect=0 disconnect=-61 restored=0 pinned=1 result=0`。
4. **桥加载**：双门控 `mt_pvr_bridge.ko` insmod，`/dev/dri/renderD128` 就绪。
5. **mt-ta-readback 全链路**：
   - connect（bvnc=0x23000406600017）→ render context handle=0x1000
   - 11 BOs + 12th target BO（va=0x7b000000 bytes=16384）
   - **13th RgnHeader BO（va=0x7c000000 bytes=4096，0xFF 预填）** ← r431 新增
   - 0xFD 提交（Header-only：`buf+0x10`=0x7c000000，`n_entries`=0）
   - fence 分配 → `dma_fence_wait_timeout(5s)` 返回 0 → **-ETIMEDOUT**。

## Key comparison

| 轮次 | TA Header +0x10 | 结果 |
|---|---|---|
| r414 | 0（全零 Header） | ✅ 0x100，219µs（"无工作"快路径） |
| r425 | 0x7b000000（16KB 像素 BO） | ❌ 超时（像素当 RgnHeader 解析） |
| r432 | 0x7c000000（RgnHeader，0xFF） | ❌ 超时（RgnHeader 非充分） |

## Analysis

r430 的三跳链（+0x10=RgnHeader VA）已活体确认正确路由——dmesg 显示
RgnHeader BO 正常绑定，提交路径走通（fence 已分配）。但固件仍不完成，
说明：

1. `+0x28`/`+0x30`（r431 诚实边界 [UNKNOWN]，置零）可能为必需字段；
2. RgnHeader 全 0xFF 可能非固件期望内容（UMD 行为 [MEASURED]，
   非固件要求证明；per-dword 语义 [UNKNOWN]）；
3. 或固件需要 MLIST 等其他结构。

**RgnHeader [INFERRED]→仍未 [MEASURED]**：本次活体是证伪性证据
（RgnHeader alone 不充分），不是对 RgnHeader 语义的否定。

## Teardown

- pending TA fence 持有 bridge ref=1 → `scripts/safe_rmmod.sh` **正确拒绝**
  （未用 `-f`，遵守红线）。
- 桥仍在载（双门控测试构建），probe ref=1 未动；
  dmesg 零 WARN/BUG/Oops（仅 boot 期 CPU 通告）。
- **待用户第 6 次冷重启清除**（同 r406/r418/r421/r425 前例）。
- 源码已 revert（`kernel/mt_ta_real.h` + `userspace/mt-ta-readback.c`
  恢复 committed 状态）；默认门控重建 W=1 零警告；工作区干净。

## Gates

- `make -C mt-vgpu-guest check-offline`：**554 Python + 1490 C 全绿**
- `make kernel` W=1：**零警告**（双门控测试构建 + revert 后默认构建）

## Deliverables

- 本报告 `reports/r432-rgnheader-live-still-timeout.md`
- 证据 `build/traces/r432/dmesg-r432.txt`（0600）+ 双源文件 `.orig` 备份
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r432（§4：r430 节已移入归档）
- `PROGRESS-SNAPSHOT.md` §12 追加 r432

## Honest boundaries

- RgnHeader 语义仍 [INFERRED]；本次为证伪性证据。
- T2 像素回读仍 open（固件未完成，无像素可读）。
- 生产代码零行为变更；双门控默认关闭。
- 单发测试；稳定性/重复性未测。
- **下一步必须离线**：确定 `+0x28`/`+0x30` 语义与 RgnHeader per-dword 要求。
  **不再做无离线依据的活体试探**（r380/r418/r421/r425/r432 教训）。
- 本轮零 `rmmod -f`、零自行重启；未链入下一轮。
