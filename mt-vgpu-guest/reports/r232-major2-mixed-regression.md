# r232：translator 混合 kick DDK2 回归（批准执行）——`=2` 下 8 步全过

- **结论**：translator 与 drm_major 正交在活体证实：桥以 `drm_major=2 translate_kick=1` 重载（probe 未碰），`pvr_update_writeback`（双 PMR 版）8 项全 ok——混合 fire（check=2 + update=1）**45.1ms** 即过（与 legacy 的 44.8ms 同构），写回 probe 0.10ms 即过；dmesg `check=2 update=1 tag=1 fence=11` → `check=1 update=0 tag=2 fence=12`（fence 序列延续）。拆桥 `unloaded cleanly`（probe 25→1）；桥恢复默认 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r232] major2-mixed-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_kick=1`（probe 未碰），节点仍 `renderD128`。
3. 活体：raw 工具与 UMD 选路无关，直接发 bridge 调用；8/8 ok，exit 0。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 只证明 translator 路径与 major 无关；r228 的 UMD 侧 DDK2 断点依然存在。
- 本轮 fresh file 自包含 + 全 teardown；未用 `timeout` 包裹。
- 无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）。
