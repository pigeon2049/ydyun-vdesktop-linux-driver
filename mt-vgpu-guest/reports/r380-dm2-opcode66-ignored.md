# r380: DM2/0x66 被 firmware 忽略（无事件），trial 会话被清除

## 结论

**DM2（3D）+ opcode `0x66` 的单发空 marker 被 firmware 忽略**：2 秒内无任何完成事件（`-ETIMEDOUT`）。与 TA 的 DM3/0x66（接受，`0x100`）形成鲜明对比。**副作用**：被忽略的 marker 导致 firmware 清除了 trial 会话状态（`0x890`: 2→0），后续测试无法进行。本轮按安全协议停止。

## 1. 测试过程

### 1.1 探针模块（`mt_live_3d_probe.ko`，一次性，未入库）

仿 r365 模式的单发探针：
- 取 S3000 marker store，`ms->ready`/`work_ready` 置位（仿 `mt_live_3d.c`）
- 构造最小命令包：opcode@0x0c、`wire_id`@0x48（r365 布局）
- `mt_fw_queue_try_submit(ms->queue, 2, 0, packet)` 提交到 DM2
- 轮询 DM2 事件环 2 秒，匹配 `words[2]==wire_id` 的事件
- 无论成败，清理 marker（摘链、signal fence）、恢复 flags、卸载

### 1.2 测试 1：DM2 + opcode 0x66

```
[ 1751.777391] mt_live_3d_probe: submitted DM2 marker opcode=0x66 wire=1
[ 1753.774649] mt_live_3d_probe: no event for wire=1 within 2000ms (firmware ignored/rejected)
[ 1753.774661] mt_live_3d_probe: done result=-110
```

- 提交成功（队列接受）
- **2 秒内无事件**：firmware 未返回完成、未返回 FAULT、未 NAK——直接忽略
- 结果 `-ETIMEDOUT`（-110）

### 1.3 测试 2：DM2 + opcode 0x64（对照，未执行）

```
[ 1765.902219] mt_live_3d_probe: HW not ready: guest=0 fw=0 started=1
[ 1765.902579] mt_live_3d_probe: init failed result=-112
```

- 前置检查失败：`0x890=0`（无 session）、`fw_state=0`，但驱动 `trial.started=1`
- **Trial 会话已被 firmware 清除**，无法继续测试
- 按安全协议停止，不再试其他 opcode

## 2. 发现与分析

### 2.1 Opcode 发现结论

| DM | Opcode | 结果 | 完成码 |
|---|---|---|---|
| 3 (TA) | 0x66 | 接受 | 0x100（r365） |
| 3 (TA) | 0x64 | 接受 | 0（标准 COMPLETE，r365） |
| 2 (3D) | 0x66 | **忽略**（无事件） | N/A |
| 2 (3D) | 0x64 | 未测（trial 中断） | N/A |

**`0x66` 不是 3D 的有效 opcode**（或 DM2 不接受最小 marker）。可能原因：
1. 3D 需要不同的 opcode（`mt_work_opcode()` 的 type→opcode 映射针对完整 work 命令，不适用于最小 marker）
2. DM2 需要完整的 3D 上下文（`mt_live_3d.c` 的 11 BOs + CSW），最小 marker 格式不对
3. DM2 的 marker 协议与 DM3 不同

### 2.2 Trial 会话清除（副作用）

被忽略的 DM2/0x66 marker 导致：
- `0x890`: 2 → 0（firmware 清除 session）
- `fw_state`: 2 → 0
- 驱动 `trial.started` 仍为 1（未感知）

推测：firmware 对 DM2 收到未知 opcode 时触发了会话重置（看门狗或错误处理）。**教训**：DM2 的 opcode 试探有副作用，不宜在 trial 会话上直接试。

### 2.3 与 r365 的对比

r365（TA）：
- DM3/0x66：接受，`0x100`
- DM3/0x64：接受，标准 COMPLETE
- 无副作用，trial 保持

r380（3D）：
- DM2/0x66：忽略，无事件
- 有副作用，trial 被清除

**3D 路径与 TA 路径的 firmware 行为显著不同。**

## 3. 安全与收尾

- 无 oops、无 hang、无 WARN/BUG
- 探针模块已卸载，无残留
- `mt_pvr_bridge` ref 0、`mt_guest_probe` ref 1（未动）
- **Trial 会话已损坏**（firmware 侧清除，驱动侧未感知）：后续 TA/3D 提交将失败，需恢复（预计需冷重启，因 warm reboot 不重置 firmware）
- 本轮**未重载**任何模块（按任务要求）

## 4. 建议的下一步

1. **系统恢复**：冷重启恢复 trial 会话（用户执行）
2. **3D opcode 研究**：从 Windows KMD 或 `mt_live_3d.c` 的完整提交路径找 3D 的真实 opcode，而非试探最小 marker
3. **DM2 协议**：研究 DM2 是否需要完整上下文才能接受命令（参考 `mt_live_3d.c` 的 `submit_context` 路径）
4. 若必须试探，应在**隔离环境**（非 trial 会话）或接受 trial 重建成本的前提下进行

## 证据

- `r380-dmesg.txt`：探针 dmesg 日志（提交、超时、HW not ready）
- 探针源码：`build/traces/r380/mt_live_3d_probe.c`（gitignored，未入库）
