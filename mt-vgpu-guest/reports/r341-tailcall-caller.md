# r341：调用方是尾跳——app→QueueTransferNew→`jmp TQJobSubmit`（批准执行）

- **结论**：GDB 文件脚本单发真实 copy tq-perf（`=2` 窗口），JobSubmit 入口 `x/gx $rsp` 得返回地址 `0x55555555802b`——归属 app 二进制 `0x402b`，恰为 `call RGXTDMQueueTransferNew@plt` 的下一条。离线配平：`RGXTDMQueueTransferNew+0x46` 是 `jmp TQJobSubmit`（直接尾跳，帧复用）。调用方点名完成：**app 静态 copy-setup 函数（`0x3fxx` 一带，`rep stos` 清零 `0x104` 个 qword 后调 QueueTransferNew）→ 尾跳进 `TQJobSubmit`**。连带解释三个历史谜题：`bt` 三轮静默（尾跳不长帧，unwinder 无链可走）、r317 core 仅两帧、r321 尾跳风格——同源。`=2` 拆 → 默认回（major=0 已验）→ L3 双绿；refs 1/0，窗口零新增 WARN。**Freeze 已恢复。**无内核代码改动（窗口脚本 `scripts/retaddr-window.sh` 落库）。
- **生产者位置再收敛**：job/ctx 空壳由 app 侧 `0x3fxx` 代码清零并传入（`0x400b` 的 `rep stos` 即出生证），计数槽在 app 侧亦未填——“缺失的生产者”就是 copy-setup 自身对该表的组装（`rep stos` 后无回填），与 Staging/bridge 无关。

## 实测（执行过）

1. 批准：用户“继续”（接 r340 既定下一刀）+ 真机/重载授权延续。预检 ref 0、无持有 → `drm_major=2` 重载 → GDB 单发（`timeout -s KILL 120`，即时 abort）→ 返回地址 + `info symbol`（app 剥符号，无名，以偏移指代）+ 离线双向配平 → 恢复默认 + L3。
2. 证据：`r341-retaddr.txt`（0600：RETADDR/`x`/`info symbol`）+ `r341-tqperf.jsonl`（0600，8731 行）；暂存区已清空。门禁沿用（394+299）。

## 边界与下一步

1. 下一刀（二选一）：① app `0x3f00–0x4030` 对齐反汇编，找 `rep stos` 后对计数槽的回填缺口（离线，零触碰）；② 同脚本断 app `0x4026` 读 QueueTransferNew 入参（活体一发）。建议先①。
2. 本轮未停桌面（无持有即不停）；teardown 前复验 ref 0（未触发挡回）。
