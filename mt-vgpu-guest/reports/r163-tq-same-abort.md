# r163：`musa_tq_performance_test` 同样倒在 copy-setup 同一 abort 点，非新 producer

- **结论**：`musa_tq_performance_test -device 0 -n 1 -width 64 -height 64`（major 2 + shared backing）在 510 行 trace 后以 SIGABRT 退出，无 `0x89:0xa`、无 `ccb_resolve`；GDB 栈与 r162 的 copy-blit 逐帧一致（同 aborter `0x…2c8d1`、同 `TQJobSubmit+738` 返回地址、同“`0x500d000` 映射后、Submit 前”位置）——同一缺失前提、同一阻塞点，不是新 producer。至此手头三个候选（fill `-n 1/2` 同一样本；copy-blit；tq-perf）均不能给出第二个 CCB 样本。本轮零硬件触碰、无代码改动。

## 实测

1. fabricated 重放（`timeout -s KILL 20`，进程 abort 退出 rc=134）：trace 止于 `0x6:0x9 → 0x6:0x15 → 0x6:0x13 → mmap 0x500d000` 之后，与 r162 nofill 的 506/505 行同形。完整证据见 [`r163-tq-abort.jsonl`](r163-tq-abort.jsonl)。
2. 离线 GDB（`-g` 同源 shim，仅 `/tmp`，未入库）：`Program received signal SIGABRT`，`#3 0x…2c8d1 (libsrv) ← #4 TQJobSubmit ← musa_tq_performance_test`——PC 与返回地址和 r162 blit-copy 轮完全相同。按名下断点（`TQJobSubmit+733`）因该符号在动态符号表不可见而保持 pending 未命中，遂止损（绝对地址断点在库加载前同样无法插入；继续钻 GDB 机制性价比低）。
3. 门禁：无代码改动，`make -C mt-vgpu-guest check-offline` 复核全绿（269 Python，1 skip + 272 C）；`lsmod` 无 `mt_*`。

## 边界

- “同一 abort 点”依据为 PC + 返回地址 + trace 位置三重一致；abort 叶内部条件（r162 已见 `0x8(%rdi)==0` 检查与格式分发）未再深挖。
- shim 回包仍 fabricated；不证明执行；不重载硬件 bridge。

## 下一步

- producer 线暂止（fill 单样本 + 两 copy 系 abort）；CCB 翻译范围维持 r160–r161 结论。候选：STATUS 步骤 2（需可重建会话批准）或把 copy-setup 缺失前提单独立项（先补源面创建链 fabrication，再谈第二样本）。
