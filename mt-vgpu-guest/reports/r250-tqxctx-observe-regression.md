# r250：observer 在 tqx_ctx 桥下回归（批准执行）——开关正交最后一格

- **结论**：observer 与 `translate_tqx_ctx` 开关正交在活体证实：桥以 `drm_major=2 translate_tqx_ctx=1` 重载（probe 未碰），`pvr_observe_ping` 全套 23 ok + PASS，CCB 窗口行 `nonzero=107` 与各桥配置下一致。observer × {默认, ck, =2, transfer, tqx_ctx} 正交矩阵补完。拆桥 `unloaded cleanly`（probe ref 1 全程未动）；桥恢复默认。**Freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0；dmesg 打 `[r250] tqxctx-ping-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_tqx_ctx=1`（probe 未碰），节点仍 `renderD128`。
3. 活体：在 `mt-vgpu-guest/` 下运行，23 ok + PASS，exit 0。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥（R_W soak 在默认桥，见 r251）。

## 边界

- 只证明开关无干扰；tqx_ctx bring-up 本体由 r219/r230 覆盖。
- 本轮 fresh file 自包含 + 全 teardown；未用 `timeout` 包裹。
- 无代码改动、无需门禁重跑。
