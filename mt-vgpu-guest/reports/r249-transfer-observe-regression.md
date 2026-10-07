# r249：observer 在 transfer 桥下回归（批准执行）——开关正交补格

- **结论**：observer 与 `translate_transfer` 开关正交在活体证实：桥以 `drm_major=2 translate_transfer=1` 重载（probe 未碰），`pvr_observe_ping` 全套 24 项全 ok（含负向双测），CCB 窗口行 `nonzero=107` 与各桥配置下一致。拆桥 `unloaded cleanly`（probe ref 自归 1）；桥恢复默认 + L3 全绿（node + smoke），dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. `rmmod`（ref 0）→ `insmod drm_major=2 translate_transfer=1`（probe 未碰），节点仍 `renderD128`；dmesg 打 `[r249] transfer-ping-start` 标记。
2. 活体：在 `mt-vgpu-guest/` 下运行，24/24 ok，exit 0。
3. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 只证明开关无干扰；transfer dry-run 本体由 r226 覆盖。
- 本轮 fresh file 自包含 + 全 teardown；未用 `timeout` 包裹。
- 无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
