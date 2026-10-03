# r120：加载窗口 runbook（命令级，待批准执行）

把"加载窗口"从一句话变成可执行清单。本轮只写文档 + 离线验证
（worktree 构建），**未执行任何加载动作**，零硬件触碰。

## 关键发现：bit-identical 回滚不存在

- 在盘桥（`a424a1cd…`，含 TDM）≠ 在载桥（`894faf50…`）。
- r63 的回滚目录（`/tmp/opencode/bridge-rollback-r60/`）已随 `/tmp`
  清理消失；worktree 重建 da3df8b（r63 代码基线）编出的是第三个
  build-id（`f8bf0d1c…`）——在载构建不可从 git 历史 bit 复现
  （当年带未提交修改或不同 flags 上机）。
- 故回滚只能是**功能级**（revert TDM hunks + 重编，重验 L3/L4），
  不是 bit 级。本 runbook 按此编写，不承诺做不到的事。

## 执行序列（批准后照单执行，任一步红灯即停）

0. 前置（只读）：`pending=0`、`objects` 记录、`refcnt 38/1/0`、
   `df -h /tmp`、dmesg 打标记行。
1. `sudo -n rmmod mt_pvr_bridge`（bridge ref 由 Chrome 持有——
   先确认 Chrome fd 仍只是 passive open；rmmod 失败则停）。
2. `sudo -n insmod kernel/recovery/mt_pvr_bridge.ko`（即 `a424a1cd`）；
   核对 `/sys/module/mt_pvr_bridge/notes` build-id。
3. L3：`./build/probe/pvr_node_probe` + `pvr_dma_smoke` + `pvr_kick_probe`
  （手跑，不用 `make probe` 整套；无 timeout 包裹）。
4. L4 简化：rung1–rung6 + rung8（跳过 compute/r7；rung8 零 count 基线）。
5. fabricated Rogue2D：`0x89:0x5` OUT 非零即赢（H1 第一判据）。
6. passthrough Rogue2D + `UMD_DUMP_BRIDGE=0x89:0x5`（H1 终判）。
7. 任一步失败 → 回滚：revert TDM（`git revert 95a7492` 的桥部分）、
   重编、rmmod/insmod、重跑 L3 全绿；仍红 → 停手等重启窗口
   （禁止反复 rmmod/insmod 赌运气，r67 教训）。

## 回滚位（功能级）

- 源码态：`95a7492` 前桥代码 = `da3df8b` 版 + r116 头改动
  （`d3b7453` 不碰桥行为，保留）。
- 验证态：L3 全绿（r63 在 `894faf50` 上的结果存档于 r63 报告）。
