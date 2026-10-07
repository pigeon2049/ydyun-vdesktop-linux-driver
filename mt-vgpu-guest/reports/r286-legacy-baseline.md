# r286：当前构建+会话 legacy 基线（无重载，零状态变更）——拒绝点仍在 `0x89:0x0`

- **结论**：默认桥（r263 构建在载）上真实 `musa_blit_test -device 0 -f -o`（passthrough 记录）：8205 行 trace，101 条 bridge 调用中 100 条 ret=0，唯一非零是 `0x89:0x0`（40/12）→ `-25`（seq 8180），随后有序拆除（unmap/unreserve/destroy/disconnect 全 0），UMD SIGABRT（134）。与 r244（旧构建+旧会话）同形：legacy 拒绝点在当前构建+会话复核成立，无 submit3（legacy 路径预期内）。refs 全程 1/1，窗口零新增 WARN。**无重载、无提交，freeze 继续。**无代码改动。

## 实测（执行过）

1. 批准依据：用户本轮指令“继续真机推进”（本轮零重载：只跑 blit 进程，不 insmod/rmmod）。预检：refs 1/1，节点 card0/card1/renderD128；UMD SHA `b3058c02…` 对版。
2. `UMD_SHIM_PASSTHROUGH=1 + LD_PRELOAD=umd_bridge_shim.so + UMD_TRACE=build/traces/r286/` + `timeout -s KILL 100`：进程即时 134（未触发 KILL）；trace 8205 行（与 r244 同行数）。
3. 解码（执行）：101 bridge 调用逐项列出——建链（connect/device/memctx/render/sync）、堆表、PMR maps、`0x89:0x5`（ret=0）各就各位；`0x89:0x0` → `-25` 后 UMD 中止，未到 submit。证据 `reports/r286-legacy-baseline.jsonl`（0600，全量）。
4. 暂存区已清空。门禁状态沿用 r285（366+292，无代码改动）。

## 边界

- 本轮只覆盖 legacy 路径；DDK2 路径需 `drm_major=2` 重载（renderD128 被占，仍不可行）。
- 拒绝是设计行为（桥无 legacy transfer handler），不是缺陷；与 r244/r260 口径一致。

## 下一步（候选）

1. 合并策略 recon（离线）：fire 合进 bridge vs 保持独立（现状 bridge 不可重载是决定性约束）。
