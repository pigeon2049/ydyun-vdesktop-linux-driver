# r344：`QueueTransferNew` 是特性门分发器——`rdx+8` 活体互证路径（离线，零硬件触碰）

- **结论**（`RGXTDMQueueTransferNew @ 0x614e0` 对齐反汇编）：序言把 app 入参存入 callee-saved 寄存器（`r14=rcx`，`r13=rsi`，`r12=rdx`，`rbp=rdi`），读 `job+8→GetFeatures`，判 `features+0x54`：
  - `>1`（`=2` 窗口）：`rdi=rbp/rsi=r13/rdx=(r12?r12+8:r12)`，`jmp TQJobSubmit`（`0x61526`，尾跳，帧复用）；
  - `≤1`：`jmp RGXTDMQueueTransfer`（`0x658c0`，legacy 路径）。
- **活体互证（r342×r338）**：app 传 `rdx=0x…d640`（r342 实测），`r12≠0` 故 `cmovne` 取 `r12+8`，而 r338 的 JobSubmit 入口 `rdx=0x…d648`——**差值恰为 8**，路径与寄存器传递逐位坐实。`rdi/rsi` 原样透传（`rdi=job 0x…b830`，`ctx=*(job+0x10)`，与 r339 的 `0x600f5` 交接吻合）。
- **行为分裂的解释**：`=2` 窗口走新路径进 `TQJobSubmit` 才撞空表断言；默认桥（feature≤1）走 legacy `RGXTDMQueueTransfer`，根本到不了 TQ setup——这正是 r286（legacy 止于 `0x89:0x0→-25` 无 abort）与 r317（`=2` 进 TQ 即 abort）的分野在 UMD 侧的根因。门控在 `GetFeatures+0x54`，与 r134（DRM `version_major==2`）/r78（桥故意钉死 legacy）首尾相接。
- **ctx 空壳的最后归属**：job（`0x…b830`，`+0x10=ctx`）由 app 在 `0x4026` 之前备好；ctx 链（`…1380→…08f0`，计数 0）随 job 一并传入。结合 app 导入表（含 `RGXTDMCreateTransferContext`），空壳极可能是 transfer-context 创建时分配、而 copy-setup 从未填充 surface 列表——候选定位到 app `0x3fxx` 或 `CreateTransferContext` 语义，需再下一刀（`CreateTransferContext` 的建表契约，或 `rdx` 零结构体的消费端）。

## 实测与边界

1. 本轮零硬件触碰：未加载、未重载、未跑 UMD；`bridge ref 0` / `probe ref 1` 不变，会话 freeze 继续。
2. 证据：`0x614e0–0x61547` 对齐反汇编（序言 + 双分支 + 双尾跳）；r342/r338 活体寄存器值交叉验证（`+8` 差值）。
3. 推断与实测的界限：特性门取值（`=2`→新路径）由活体路径反推，未直接读出 `GetFeatures+0x54` 的值；`ctx` 空壳出自 `CreateTransferContext` 系候选，未证实。

## 下一步

1. `RGXTDMCreateTransferContext` 的建表契约（离线）：它分配 ctx 时是否初始化计数/列表，copy-setup 是否跳过了某绑定调用（与 fill 路径 `musa_blit_test -f` 的调用序列逐项对照——fill 走通 setup，它的序列即“完整版”参照）。
