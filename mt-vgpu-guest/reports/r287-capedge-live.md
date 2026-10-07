# r287：分块上限边界活体验证（批准执行）——64/64 块全绿

- **结论**：`MT_FIRE_MAX_CHUNKS=64` 上限边界（4096×1024，16 行/块恰好 64 块，单块 256KB 顶满 scratch）活体全绿：`fired=1 chunks=64 verified=1 bad=0/4194304 first=last=0xff000000 result=0`。64 fence 逐个 signal，无等待超时。拆模块干净，refs 回 1/1，窗口零新增 WARN。**bridge 未碰，freeze 继续。**无代码改动。

## 实测（执行过）

1. 批准依据：用户本轮指令“继续真机推进”（动作类别同 r283–r286）。开工先查 holder：renderD128 仍被重启后的桌面（PID 65770）持有，bridge 重载继续不可行——故仍走独立模块。
2. 离线先验算（`rows=16`、`16*16384=262144=SCRATCH`、`nchunks=64` 恰顶上限、`pixels=4194304`），再上机。
3. `insmod enable=1 width=4096 height=1024 color=0xff000000` rc=0 → bring-up OK；`run=1` rc=0，约 11 秒出 fired 行（64 块串行，每块均在 5s 预算内，无 fence 超时行）。
4. `rmmod` rc=0，节点消失，refs 回 1/1；窗口 WARNING/BUG/Oops 计数 0。
5. 证据 `reports/r287-capedge-live.dmesg`（0600，6 行）；暂存区已清空。门禁状态沿用 r285（366+292，无代码改动）。

## 边界

- 上限以上（65+ 块，如 4K 全帧）仍只在源码门禁层拒绝（`-E2BIG`），未活体触发——触发即失败路径，本轮未烧这个窗口（上限内执行优先）。
- 三轮几何（1280×1024、1920×1080、4096×1024）+ 三种颜色 + 5 轮 soak 均已闭环；独立模块执行侧已无未验证项。

## 下一步（候选）

1. 合并策略 recon（离线）：fire 合进 bridge vs 保持独立收敛为正式工具（bridge 不可重载是决定性约束，建议后者）。
