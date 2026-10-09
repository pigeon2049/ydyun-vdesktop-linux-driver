# r421: Q0 修正后活体——提交成功但固件仍超时（Q1 编码待深挖）

> Round r421 (2026-10-09). **最高风险轮。** r419 修正（Q0=flags-only，Q1=target_va）后的首次活体验证。

## Conclusion

**修正后的 Q0/Q1 编码提交成功，但固件 5s 内无完成事件（-ETIMEDOUT，submitted-but-ignored）。Q1=target_va 未能从 [INFERRED] 提升为 [MEASURED]。**

1. **双门控测试构建**：`MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`（static_assert 临时中和为 ==1，事后 revert）；`make kernel` W=1 **零警告**。
2. **Pre-live T1/T2/T3/T4**：13 tests 全过（含 T4 Q0 纯净性门禁）。
3. **Trial 重建**：冷重启后 probe 全参数链加载（enable_probe query_info probe_rpc reserve_memory prepare_resources load_firmware trial_connect runtime_context），dmesg：`firmware trial: connect=0 disconnect=-61 restored=0 pinned=1 result=0`。
4. **桥加载**：双门控 `mt_pvr_bridge.ko` insmod，`/dev/dri/renderD128` 就绪。
5. **mt-ta-readback 全链路**：connect（bvnc=0x23000406600017）→ render context handle=0x1000 → 11 BO 绑定 + 12th target BO（va=0x7b000000 bytes=16384，r416 设计活体确认）→ 0xFD 提交（Q0=0x48000000000 flags-only，Q1=0x7b000000）→ fence 分配 → `dma_fence_wait_timeout(5s)` 返回 0 → `-ETIMEDOUT`。
6. **对比**：
   - r414：Q0=0/Q1=0 → 0x100 完成，219us。
   - r418：Q0=`va|0x48000000000`（flags 污染）→ 超时。
   - r421：Q0=flags-only/Q1=target → 超时。
   
   **结论**：Q0 污染不是唯一的超时原因；即使 Q0 干净、Q1 携带 target VA，固件仍不完成。要么 Q1≠target（推断错），要么 TA 条目其他字段（Q2/Q3/Q4）或 DM 包布局另有问题。

## Live evidence

dmesg（`build/traces/r421/dmesg-r421.txt`，0600）：
```
[ 1848.977622] mt_guest_probe 0000:00:0e.0: firmware trial: connect=0 disconnect=-61 restored=0 pinned=1 result=0
[ 1862.560240] mt_pvr_bridge: registered 'pvr' node, bridge stage 1: main module still owns the device (no binding)
[ 1908.548379] mt_pvr_bridge: r389: render ctx VM created, base_va=0x70000000
[ 1908.552602] mt_pvr_bridge: r416: target BO bound va=0x7b000000 bytes=16384
[ 1908.552606] mt_pvr_bridge: r389: render context READY (11 BOs, CSW, exec)
```

userspace：
```
[*] connected bvnc=0x23000406600017
[*] render context handle=0x1000
check failed line 161: 0 errno=110   (ETIMEDOUT)
```

注：0xFD handler（`pvr_cmd_ta_readback`）无 dmesg 日志（设计为静默）；errno=110 证明提交路径走通（fence 已分配），仅完成事件缺失。

## Teardown

- pending TA fence 持有 bridge ref=1 → `scripts/safe_rmmod.sh` **正确拒绝**（未用 `-f`，遵守红线）。
- 桥仍在载，probe ref=1 未动；dmesg 零 WARN/BUG/Oops（仅 boot 期 CPU 缺陷通告）。
- **待用户冷重启清除**（同 r406/r418 前例）。
- 源码已 revert（`kernel/mt_ta_real.h` 恢复 committed 默认门控）；默认门控重建 W=1 零警告；工作区干净。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**543 Python + 630 C 全绿**（1 skipped）
- `make kernel` W=1：**零警告**（双门控测试构建 + 默认构建）
- 反向验证：未单做（T4 门禁在 r420 已反向验证；超时本身即对 Q1 推断的证伪信号）

## 交付物

- 本报告 `reports/r421-q0-corrected-still-timeout.md`
- 证据 `build/traces/r421/dmesg-r421.txt`（0600）
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r421（§4：r419、r418、r417 节已移入归档）
- `PROGRESS-SNAPSHOT.md` §12 追加 r421

## 诚实边界

- **Q1=target_va 仍 [INFERRED]**：本次活体是证伪性证据（firmware 不完成），不是确认。不能排除"Q1 对了但别的字段错了"。
- T2 像素回读仍 open（固件未完成，无像素可读）。
- 生产代码零行为变更；双门控默认关闭。
- 单发测试；稳定性/重复性未测。

## 下一步

1. **P0（离线）**：深挖 Q1/target 语义——`RGXPrepareTA` 从缓冲偏移 0x10/0x18/0x20/0x28/0x30/0x38/0x40/0x48/0x60 回读 9 个 qword（decompiled.c:54365-54374），对照 psKickTA 字段表（r408），确定哪个 qword 槽位才是 firmware 认的 render target。
2. **P1（离线）**：检查 TA 条目 Q2（维度打包）/Q3/Q4 的 dummy 值是否会导致 firmware 静默丢弃；对照 `FUN_00169240` 的 Path A/B 条件位。
3. **P2（活体）**：Q1 语义明确后，重跑 `mt-ta-readback` 验像素。**不再做无离线依据的活体试探**（r380/r418 教训）。
