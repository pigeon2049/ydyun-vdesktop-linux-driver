# r289：超限矩形 `-E2BIG` 活体拒绝（批准执行）——零提交，门卫生效

- **结论**：3840×2160（128 块 > 64 上限）`run=1` 即拒：`fired=0 chunks=128 verified=0 bad=0/0 result=-7`（`-E2BIG`），dmesg 无任何 chunk prepare/submit/fence 行（一次 GPU 提交都未发生），`rmmod` 干净，refs 回 1/1，窗口零新增 WARN。模块至此正反分支活体全覆盖。**bridge 未碰，freeze 继续。**无代码改动。

## 实测（执行过）

1. 批准依据：用户本轮指令“继续真机推进”（动作类别同 r283–r287）。离线先验算（`rows=17`、`nchunks=128>64` 必拒）。
2. `insmod enable=1 width=3840 height=2160` rc=0 → bring-up OK（拒绝发生在 fire 数学层，bring-up 不受影响，符合“大声拒绝晚期输入”语义）。
3. `run=1` rc=1（预期非零）；`chunks=128` 记录的是尝试块数（`fired=N` 证明未发）。`rmmod` rc=0，节点消失。
4. 证据 `reports/r289-e2big-live.dmesg`（0600，6 行）；暂存区已清空。门禁状态沿用 r288（366+292，无代码改动）。

## 边界

- 本轮是模块最后一个未验证分支；执行侧正（3 几何 + soak + 上限）反（超限拒绝）分支至此全部活体覆盖。
- 单行超限（`row_bytes > SCRATCH`）同属 `-E2BIG`，与本轮同 errno 不同判据，未单独烧窗口（源码门禁已断言，活体行为同构）。

## 下一步（候选）

1. holder 窗口（需用户协调）：bridge 三开重载 + 真实 DDK2 blit 的 UMD 驱动 fire（含 r280 串行移植修）——STATUS #1 真实绘制的唯一剩余路径。
