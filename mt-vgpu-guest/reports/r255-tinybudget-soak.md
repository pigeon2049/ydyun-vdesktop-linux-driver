# r255：短预算下 10 轮 soak（批准执行）——预算×复用组合

- **结论**：同窗口（`translate_kick=1 translate_wait_ms=100`，未重载）10 轮混合 fire **10/10 通过**，0.12ms/轮起（prepare 常驻复用 + 短预算，命中远小于预算）；dmesg 20 行 translated 全落（tag 连续至 19/20，fence 至 79/80，无跳号）；零 WARNING/BUG/Oops。预算×复用组合成立：短预算不影响复用路径。拆桥 `unloaded cleanly`（probe 25→1）；桥恢复默认 + L3 全绿（node + smoke）。**Freeze 已恢复。**

## 实测（执行过）

1. dmesg 打 `[r255] tinybudget-soak-start` 标记；同桥配置循环 10 次工具，10/10 exit 0。
2. 耗时分布：0.12ms 起（r241 默认预算 soak 的 0.10ms 同构——预算值不影响命中耗时）。
3. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 只证明短预算+复用组合；soak 未覆盖 UMD 链（raw 工具）。
- 未用 `timeout` 包裹；无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
