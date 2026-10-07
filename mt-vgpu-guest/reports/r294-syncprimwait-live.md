# r294：hanging 实锤为 `SyncPrimWait` 用户态 spin（批准执行）+ 有界自杀

- **结论**：`=2` 纯 observe 窗口（translator 全关）同样 hanging（137，8201 行）——hang 不需要 tqx_ctx（r214 即时 abort 系会话/构建差异，非配置差异）。hang 中活体三连实锤：① `State: R` + `wchan: 0`（用户态 spin，非 poll/futex 睡眠）；② GDB 活体栈 `SyncPrimWait() → sched_yield()`（`libsrv_um_MUSA.so`，经 `SemWaitI`）；③ 约 100 秒后有界自杀：`SIGABRT` + core（`sutu_fail_if_errorI`，wait 返回错误即 abort）。桥侧三重缺失（回 0 + 零填充 OUT + 无 fence + 不写回写 + 零像素）→ UMD 等永远不来的同步值。**Freeze 已恢复（拆桥 + 默认 + L3 双绿，refs 1/1，窗口零新增 WARN）。**

## 实测（执行过）

1. 批准：用户 standing 授权（活体自测）。手动窗口（无脚本改动，步骤收敛）：停桌面 → ref 0 → `rmmod` → `=2` → blit#1（137/8201，observe `nonzero=40` 第 12 轮值）→ blit#2 后台 hang → 检 wchan/R → GDB 活体栈 → 二次 GDB 时进程已死 → 查 core（SIGABRT 02:05:35）→ `rmmod` → 默认桥 → L3 双绿 → 拉桌面（10 进程）。
2. 活体证据（执行，非推断）：
   - `cat /proc/PID/wchan` = `0`（空，即不在任何内核等待），`status: R (running)`，`stack` 不适用（运行态）；父进程 `do_wait`（blit fork 了 worker，GDB 目标是子）。
   - GDB（`gdb -p` 活体，未破坏进程——detach 干净）：`#0 sched_yield / #1 SyncPrimWait (libsrv_um) / #2 SemWaitI / #3 blit`。
   - core 验尸：`SIGABRT`，栈顶 `sutu_fail_if_errorI ← abort`——wait 有界（约 100s）+ 出错即自杀，不是无限 hang。
3. 证据：`r294-hang.jsonl` + `r294-observe-only.jsonl`（0600，各 8201 行）+ `r294-blit-core.bin`（0600，1MB，`bt` 见上；`bt full` 无符号，args 未得）。暂存区已清空。
4. 无代码改动，门禁状态沿用 r293（366+292）。

## 边界与下一步

- 未命名（留待 r295 窗口，一次 GDB 抓参）：`SyncPrimWait` 等的 (sync, offset, expected) 三元组——当时只抓了栈，`frame 1 + info registers` 可读参（或反汇编 `SyncPrimWait` 找轮询地址计算）。trace 不记 IN 字节，此路不通。
- 满足该同步 = 写目标值到其 backing PMR（`0x2:0xa` 真写已在 r220 落桥、r222/r223 活体验值语义——拼图的最后一块：把“谁、哪、等几”三元组钉死）。
- r214 即时 abort vs 本会话等待后自杀：UMD 侧分支条件未知（两次会话行为不同），不展开；不影响“等同步值”的主结论。
