# r365：DM3 接受 opcode 0x66 marker（事件码 0x100）；对照实验证明 firmware 区分 opcode

**结论**：真机单发 TA marker（DM=3，opcode `0x66`，空 payload）被 firmware 接受处理——命令被消费、返回 wire_id 匹配的事件（`words[1]=0x100`，非 FAULT/超时/无响应）。对照组（DM=3，opcode `0x64`）得标准 COMPLETE（`words[1]=0`），证明 firmware 区分 opcode：`0x100` 是 `0x66` 命令类特有的完成码（细语义未解码）。**V1（DM3 分配）与 V2（opcode 0x66 分发层）均通过**；DM3 未被拒，无需 DM=4 回退。TA 引擎语义确认（V4）留待 R2b（需真实 payload）。

## 实测与边界

1. 全程真机活体，但只做 userspace marker 试探：两次 `insmod`/`rmmod`（`0x66` 一次、`0x64` 对照一次），每次单发一个 0x50 字节空包（opcode + wire_id + pid，其余全零），不携带渲染命令。
2. 提交绕开 marker store bookkeeping（`s->pending`/`s->count` 未碰），走 `mt_fw_queue_try_submit(s->queue, dm, 0, packet)` 直发；事件只 peek 不 ack，驱动自身的 `mt_runtime_poll` 照常 drain。
3. 安全：freeze 会话零触碰（probe/bridge 无 rmmod/insmod/unbind/改参）；refs 前后 `1/0` 不变；dmesg 无新增 WARN/BUG/Oops；未跑 `make probe`（其 WITH_BRIDGE 会 rmmod，同 r358/r360 取舍）；`timeout` 未进入任何临界区（观察窗为模块内 `msleep` 轮询）。
4. 未断言：`0x100` 的细语义（只确立：非 FAULT、非 COMPLETE、与 wire_id 匹配、良性）；`0x66` 是否为"TA 专用"语义——空 payload 下无法区分"TA 引擎处理"与"firmware 透传 ack"，需 R2b 真实 payload 验证（V4）。

## 实验设计（`mt_live_ta_marker.c`，一次性探针，未入库）

- 会话校验照抄 `mt_live_marker.c`（S3000 定位、`try_module_get`、trial pinned/connected、service running、marker store 全 idle、`s->can_submit`），另加与 `mt_guest_device` 布局断言（`sizeof==30784` 等四项，防错版）。
- 包：`+0x0c=opcode`，`+0x48=wire_id(0x65036501)`，`+0x4c=pid`；其余零。
- 观察：提交前快照 DM 命令环/事件环 head/tail；提交后 2s 内（200×10ms）轮询事件环，新事件按 `words[2]==wire_id` 归因；另记录命令环 tail 是否推进（consumed）。
- 判定：`words[1]==0`→接受（COMPLETE）；`0x101`→拒绝（FAULT）；超时→无响应；`try_submit<0`→队列层拒绝（非 firmware 裁决）。
- 模块置 `build/traces/r365/`（gitignored），W=1 零警告构建；源码见证据 `r365-module-source.txt`（0600）。未入库原因：一次性探针，硬编码实验参数，无复用逻辑；价值在观测结论，复现信息本报告已完备。

## 实测记录

### 基线（发包前）
- `mt_guest_probe` ref=1，`mt_pvr_bridge` ref=0；在载桥 build-id `0d6bb8d7…`（r356，含 observer）；无 `mt_live_*` 模块在载；dmesg 1281 行，尾部为既往轮次残留（r363 observer 日志等），无新增异常。

### Run 1：DM=3，opcode 0x66（V1/V2 主试探）
- `insmod` 成功；`try_submit` 返回 0（已入队）。
- 结果：`result=-71`（`-EPROTO`，事件码非已知 COMPLETE/FAULT）、`consumed=Y`（firmware 取走了命令）。
- 事件：`ev=[00000000 00000100 65036501 00000000 00000000 00000000]`——`words[1]=0x100`，`words[2]=0x65036501`（wire_id 匹配，归因确凿），其余零。
- 解读：firmware 即时消费并回事件，非拒绝三件套（FAULT/超时/无视）中的任何一种。

### Run 2：DM=3，opcode 0x64（对照组）
- 结果：`result=0`（COMPLETE）、`consumed=Y`。
- 事件：`ev=[00000000 00000000 65036501 00000000 00000000 00000000]`——标准 marker 完成码。
- 解读：同一 DM、同一包形，opcode 不同则事件码不同（`0x100` vs `0`）→ **firmware 在分发层区分 opcode**；`0x100` 为 `0x66` 类特有完成码。

### 收尾
- 两次 `rmmod` 均干净；`lsmod` 无残留；refs 仍 `1/0`；dmesg 在两次探针行之后零新增（`grep -ciE 'warn|bug|oops|call trace'` = 0）。

## V1/V2 判定

| # | r364 推断 | 本轮判定 | 依据 |
|---|---|---|---|
| V1 | TA 分配 DM3 | **通过** | DM3 即时消费命令并回 wire_id 匹配事件；为 live 响应引擎 DM，非死 DM |
| V2 | opcode 0x66 为 TA 命令码 | **通过（分发层）** | firmware 未 NAK/FAULT；对照实验证明 0x66 与 0x64 走不同完成路径（0x100 vs 0） |

`0x100` 细语义、V4（TA 命令缓冲 firmware 解析）、V3（`kick_pr`）留待 R2b。

## 下一步

1. R2b 设计轮：`0x82:0xC` 真实执行——TA 命令缓冲 firmware 语义（V4）+ CCB 资源闭包（r364 D8 依赖项）+ `kick_pr` 语义（V3）。
2. 可选：解码 `0x100`——从 Windows KMD 事件分发（`14000be34`）找 `0x100` 分支，或带真实 TA payload 重跑看事件码是否变化。
