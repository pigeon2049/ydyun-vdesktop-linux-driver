# r336：计数槽同路径无人写——零值是上游带来的（批准执行）

- **结论**：GDB 文件脚本单发真实 copy tq-perf（`=2` 窗口）：`+0x3320` 入口锚定（`rdi`=调用者栈局部，`rsi=0x…0240` 堆 ctx）；入口即对计数槽（`*(*(rsi+0x58)+0x20)+12`）下硬件写观察——**零命中**，之后 abort 照常发生（abort 点寄存器与 r334 同形）。计数槽在整条 `+0x3320→分发→+0x2ff0→abort` 路径内从未被写入：零值是调用前就带来的（上游 setup 未填或上游分配即零），不是同路径填充跳过。分发标志同步命名：`(r14)=0/(r14+8)=1/(r14+0xa0)=0`，走 `+0x2ff0` 分支（非 `0xb/0x2` 退出口）。`=2` 拆 → 默认回（major=0 已验）→ L3 双绿；refs 1/0，窗口零新增 WARN。**Freeze 已恢复。**无内核代码改动（窗口脚本 `scripts/dispatch-watch-window.sh` 落库）。
- **附带确定性**：abort 点 `rax/rdi` 堆地址与 r334 跨轮完全一致（`0x…08f0/0x…1380`）——堆布局确定，可做绝对地址断点，免现算。

## 实测（执行过）

1. 批准：用户“继续”（接 r335 既定下一刀）+ 真机/重载授权延续。预检 ref 0、无持有 → `drm_major=2` 重载 → GDB 单发（`timeout -s KILL 120`，即时 abort）→ 读入口/分发/观察哨/abort 点 → 恢复默认 + L3。
2. 证据：`r336-dispatch-watch.txt`（0600：ARMED/ENTRY/CSLOT/DISP/ABORTSITE）+ `r336-tqperf.jsonl`（0600，8689 行）；暂存区已清空。门禁沿用（394+299）。

## 边界与下一步

1. 下一刀：调用前的填充责任方——`+0x3320` 入口 `rdi`（调用者 `-0x230` 局部内藏指针）与 `rsi` ctx 在 `TQ_BlitInit→CheckFences→LookUpEOT` 链中的写入史；或对照 fill 路径同槽值（`musa_blit_test -f` 走通 setup，可比）。另行开轮。
2. 本轮未停桌面（无持有即不停）；teardown 前复验 ref 0（未触发挡回）。
