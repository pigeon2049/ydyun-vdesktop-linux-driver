# r425: Header-only 活体——固件仍超时（Header-only 不充分）

> Round r425 (2026-10-09). **最高风险轮。** r423 Header-only 方案的首次活体验证。

## Conclusion

**Header-only 提交（`TA_buf+0x10=target_va`，其余全零，`n_entries=0`）仍 5s 固件超时（-ETIMEDOUT，submitted-but-ignored）。Header-only 不充分——r422 的"Entry 污染 Header"不是 r421 超时的完整解释。**

| 轮次 | TA 缓冲 | 结果 |
|---|---|---|
| r414 | 全零 360B（Q0=0） | ✅ 0x100，219µs |
| r418 | Entry：Q0=`va\|flag`（污染） | ❌ 超时 |
| r421 | Entry：Q0=flags-only，Q1=target | ❌ 超时 |
| r425 | Header-only：+0x10=target_va，其余零 | ❌ 超时 |

**分析**：r414 全零 = 固件"无工作"快路径（立即完成），不是真实提交。任何非零 Header（r418/r421 的 Entry 污染、r425 的干净 Header+target）都会让固件尝试解析工作，但我们的 Header 不完整——固件很可能要求：
1. `+0x10` 指向的是 render-target **元数据结构**（RGXPrepareTA 写的是 ctx 缓冲数组元素），而非 16KB 原始像素 BO；
2. 其他 Header 字段（`+0x28/+0x30/+0x68/+0x78/+0xb0/+0xe8` VA，`+0x120/+0x140/+0x150/+0x160` flags，r422 [MEASURED]）需要有效值。

## Live evidence

dmesg（`build/traces/r425/dmesg-r425.txt`，0600）：
```
[ 2726.439827] mt_guest_probe 0000:00:0e.0: shared channel round-trip result=0 mode=1 registered=15
[ 2731.649067] mt_guest_probe 0000:00:0e.0: firmware trial: connect=0 disconnect=-61 restored=0 pinned=1 result=0
[ 2745.717130] [drm] Initialized pvr 0.1.0 for 0000:00:0e.0 on minor 1
[ 2758.903281] mt_pvr_bridge: r416: target BO bound va=0x7b000000 bytes=16384
[ 2758.903285] mt_pvr_bridge: r389: render context READY (11 BOs, CSW, exec)
```

userspace：
```
[*] connected bvnc=0x23000406600017
[*] render context handle=0x1000
check failed line 161: 0 errno=110   (ETIMEDOUT)
```

- 0xFD handler 全路径执行（connect→create→12th BO→submit→fence 分配→5s 等待→超时），`n_entries=0` 被 `mt_ta_submit_real` 接受（无 -EINVAL）。
- 0xFD handler 设计为静默（无 dmesg 日志）；errno=110 证明提交路径走通，仅完成事件缺失。

## 过程记录

1. Pre-live T1/T2/T3/T4/T5：`check-offline` **550 Python + 1416 C 全绿**（1 skipped）。
2. 双门控测试构建（`MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`，static_assert 临时中和为 ==1；userspace `n_entries` 1→0 临时）：`make kernel` W=1 **零警告**。
3. Trial 重建：冷重启后 probe 全参数链加载，dmesg `firmware trial: connect=0 ... pinned=1 result=0`。
4. 双门控桥 insmod，`/dev/dri/renderD128` 就绪。
5. `mt-ta-readback /dev/dri/renderD128 <out.ppm>` → ETIMEDOUT（单发）。
6. `safe_rmmod.sh` 拒绝（ref=1，pending fence），未强卸。
7. 源码已 revert（`mt_ta_real.h` + `mt-ta-readback.c` 恢复 committed 默认）；默认门控重建 W=1 零警告；工作区干净。

## Teardown

- pending TA fence 持有 bridge ref=1 → `safe_rmmod.sh` **正确拒绝**（未用 `-f`，遵守红线）。
- 桥仍在载（双门控测试构建），probe ref=1 未动；dmesg 零 WARN/BUG/Oops（仅 boot 期 CPU 通告）。
- **待用户冷重启清除**（同 r406/r418/r421 前例）。
- 源码已 revert 为 committed 默认门控状态；默认重建 W=1 零警告。

## 门禁

- `make -C mt-vgpu-guest check-offline`：**550 Python + 1416 C 全绿**（1 skipped）
- `make kernel` W=1：**零警告**（双门控测试构建 + 默认构建）
- 反向验证：超时本身即对"Header-only 充分"假设的证伪

## 交付物

- 本报告 `reports/r425-header-only-live-still-timeout.md`
- 证据 `build/traces/r425/dmesg-r425.txt`（0600）+ 构建备份（`.orig`）
- `reports/README.md` 主线表 +1 行
- `MEMORY.md` 顶部插入 r425（§4：r423 节已移入归档）
- `PROGRESS-SNAPSHOT.md` §12 追加 r425

## 诚实边界

- **Header-only [INFERRED]→仍未 [MEASURED]**：本次活体是证伪性证据（固件不完成）。
- r422 的"Entry 污染 Header"是真实 bug（已由 T5 拦截），但不是超时的完整解释——干净 Header 仍超时。
- 下一步必须离线：确定固件期望的 render-target 结构（`+0x10` 的真实语义）与其他必需 Header 字段。**不再做无离线依据的活体试探**（r380/r418/r421/r425 教训）。
- 生产代码零行为变更；双门控默认关闭。单发测试。
