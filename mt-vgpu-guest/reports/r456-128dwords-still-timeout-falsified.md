# r456：128 dwords RgnHeader 活体仍 5s 超时——r454"量不足"假说被证伪

**结论：r455 实现的双循环 RgnHeader 初始化（128 dwords，对齐 UMD）未能解决固件 5s 超时。r454 的"初始化量不足"假说被活体证伪。RgnHeader 方向已穷尽。**

## 系统确认

| 检查项 | 结果 |
|---|---|
| 第 15 次冷重启 | ✅ uptime 0 min（22:49） |
| 初始模块 | ✅ `mt_guest_probe`/`mt_pvr_bridge` 均未加载 |
| HEAD | ✅ `c1ca88b`（r455，已 push），工作区干净 |

## Trial 重建

直接跑 `scripts/fresh-trial.py --run --runtime-context`（r451 流程）：

```
shared channel round-trip result=0 mode=1 registered=15
firmware trial: connect=0 disconnect=-61 restored=0 pinned=1 result=0
runtime_context_published: true
```

✅ Trial 成功（pinned=1，runtime context 已发布）。

## 双门控构建

- `MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`（static_assert 临时中和）
- userspace `n_entries` 1→0（Header-only，r423）
- `make kernel` W=1 **零警告**
- 门禁：598 Python 中 596 通过，2 个预期失败（`test_gate_default_off`/`test_readback_debug_default_off`——dual-gate 期间 gate=1，属预期）
- 构建后已 revert，工作区干净

## 活体结果：5s 超时 ❌

**测试**：`./build/userspace/mt-ta-readback /dev/dri/card1 build/traces/r456/readback_out.bin`

```
[*] connected bvnc=0x23000406600017
[*] render context handle=0x1000
check failed line 161: 0 errno=110
```

dmesg 确认 13 个 BO 全部绑定（RgnHeader at 0x7c000000，bytes=4096 page-aligned），render context READY，但固件无响应。

**r454 假说被证伪**：128 dwords（UMD 全量）仍超时。"初始化量不足"不是根因。

## 假说证伪链（r453–r456）

| 轮次 | 假说 | 结果 |
|---|---|---|
| r453 | RgnHeader 未填充真实 region 数据 | 被 r454 证伪（1s 即完整初始化） |
| r454 | RgnHeader 初始化量不足（一半） | 被 r456 活体证伪（128 dwords 仍超时） |
| r456 | — | **RgnHeader 方向已穷尽** |

## 安全

- Bridge refcount 0→1（pending TA fence），r440/r451 同模式；按协议停止，未尝试卸载
- `mt_guest_probe` ref=1（retained trial）
- 双门控源码已 revert；工作区干净
- 未用 `rmmod -f`；未自行重启
- dmesg 零 WARN/BUG/Oops（5 条 grep 命中均为 benign boot 消息）
- 证据：`build/traces/r456/dmesg-r456.txt`（0600）、`build/traces/r456/readback_out.bin`

## 诚实边界

- [MEASURED]：128 dwords 初始化活体仍 5s 超时；13 BO 绑定；context READY
- [FALSIFIED]：r454"量不足"假说
- [UNKNOWN]：固件超时的真实根因；固件实际读取 RgnHeader 的方式
- 本轮未链入下一轮

## 下一步（需 parent/用户决策）

1. **P0**：用户第 16 次冷重启（bridge ref=1 pending）
2. **P1**：RgnHeader 方向已穷尽。候选方向：
   - 0x50B 包 opcode（我方 0x66 vs 真实 0x2ABC0065）——r453 记为 [TO-VALIDATE]
   - Bridge 参数缺失（`RGXKickTA3D` 的 fence/sync/RT dataset 等）——r453 评估为低嫌疑但未完全排除
   - 固件 TA 命令解析的其他前置条件
3. 不建议继续在 RgnHeader 初始化上投入
