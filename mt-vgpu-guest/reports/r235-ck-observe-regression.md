# r235：observer 在 translator 桥下的回归（批准执行）——开关正交

- **结论**：observer 与 `translate_kick` 开关正交在活体证实：同一 `=2` + `translate_kick=1` 窗口内（r234 之后，未重载），`pvr_observe_ping` 全套 22 项全 ok，CCB 窗口行 `nonzero=107 first=0x10 fnv=0x8effdedcbd0d9d57` 与 legacy/默认桥下一致。translator 开关不干扰 observer 分发与统计。拆桥 `unloaded cleanly`（probe 25→1）；桥恢复默认 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. dmesg 打 `[r235] ck-ping-start` 标记；在 `mt-vgpu-guest/` 下运行，全 22 ok，exit 0，`61 slots`。
2. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 只证明开关无干扰；DDK2 UMD 侧断点（r228）依然存在。
- 本轮 fresh file 自包含 + 全 teardown；未用 `timeout` 包裹。
- 无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
