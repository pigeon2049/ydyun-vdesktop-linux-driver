# r229：observer 全套 DDK2 回归验证（批准执行）——`=2` 下 22 步全过，三行与 legacy 一致

- **结论**：observer dispatch 与 drm_major 正交首次在活体证实：桥以 `drm_major=2` 重载（probe 未碰），`pvr_observe_ping` 全套 22 项全 ok（ping/control/envelope/fire/preset/非零/CCB/全 teardown），dmesg 三行与 legacy 下逐项一致（零窗口 nonzero=0、手塑 nonzero=20/`fnv=0xb5e3eda7f6a52a47`、CCB nonzero=107/`fnv=0x8effdedcbd0d9d57`）。附带：legacy `0x82:0x8` render create 在 `=2` 桥下同样成功（桥不拒绝 legacy create，只影响 UMD 选路）。拆桥 `unloaded cleanly`（probe ref 1，全程未动）；桥恢复默认 + L3 全绿，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r229] ddk2-observe-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2`（probe 未碰），节点仍 `renderD128`。
3. 活体：在 `mt-vgpu-guest/` 下运行 ping 工具（CCB 相对路径要求），22/22 ok，exit 0，`ccb slots planted 61 slots`。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 只证明 observer 路径与 major 无关；DDK2 的值预置断点（r228：`SetSyncPrim` 形状问题）依然存在——DDK2 非零 kick 链仍断。
- 本轮 fresh file 自包含 + 全 teardown；未用 `timeout` 包裹。
- 无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- DDK2 param_1 形状 recon（离线）；TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）。
