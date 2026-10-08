# r340：`bt` 在文件脚本 batch 下通用静默 + 调用方经指针进入（批准执行）

- **结论**：GDB 文件脚本单发真实 copy tq-perf（`=2` 窗口），`TQJobSubmit` 入口加 `bt`——**同样零输出**（r333/r334 在 abort 点，r340 在正常入口，三轮一致）。同脚本内 `printf`/`info registers`/`x/` 正常落盘，唯 `bt` 静默：文件脚本 batch 语境下 `bt` 不可用，此路放弃，改用 `x/gx $rsp` 读返回地址（r380 已验证手法）。离线配平：app 导入的是 `RGXTDMCreateTransferContext/Destroy/QueueTransferNew`（transfer API），不含 `TQ*`；lib 全二进制无直接 `call TQJobSubmit/TQJobMultiSubmit`——copy 经 `RGXTDMQueueTransferNew(0x614e0)` 进入，`TQJobSubmit` 在内部经指针/分发到达。调用方点名留待返回地址一发。`=2` 拆 → 默认回（major=0 已验）→ L3 双绿；refs 1/0，窗口零新增 WARN。**Freeze 已恢复。**无内核代码改动（窗口脚本 `scripts/jobsubmit-bt-window.sh` 落库）。
- **快照一致性**：入口六点值与 r338 同形（job `0x…b830`、ctx 链、`cnt=0`），跨轮稳定。

## 实测（执行过）

1. 批准：用户“继续”（接 r339 既定下一刀）+ 真机/重载授权延续。预检 ref 0、无持有 → `drm_major=2` 重载 → GDB 单发（`timeout -s KILL 120`，即时 abort）→ 入口快照 + `bt`（静默）+ abort 点 → 恢复默认 + L3。
2. 证据：`r340-bt-silent.txt`（0600）+ `r340-tqperf.jsonl`（0600，8733 行）；暂存区已清空。门禁沿用（394+299）。

## 边界与下一步

1. 下一刀（活体一发）：JobSubmit 入口 `x/gx $rsp` 取返回地址 + `info symbol` 点名调用方；窗口脚本复用。另行开轮。
2. 本轮未停桌面（无持有即不停）；teardown 前复验 ref 0（未触发挡回）。
