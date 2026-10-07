# r284：大矩形分块通用性活体验证（批准执行）——32 块/绿像素全绿

- **结论**：`mt_live_tqx_fire` 换参（1920×1080/`0xff00ff00`）重载→bring-up→单发→拆卸：`fired=1 chunks=32 verified=1 bad=0/2073600 first=last=0xff00ff00 result=0`，与离线预言（34 行/块、32 块、2073600 像素）逐项一致。颜色参数真实落地 GPU（与 r283 红值区分）。拆模块干净，refs 回 1/1，本轮窗口零新增 WARN。**bridge 未碰，Freeze 继续。**

## 实测（执行过）

1. 批准依据：用户本轮指令“继续真机推进”（动作类别与 r283 已批一致：只增卸独立模块，不碰 bridge/probe）。离线先验算分块数学（`rows=34`、`nchunks=32`、`last=199680B≤256KB`），再上机。
2. `insmod enable=1 width=1920 height=1080 color=0xff00ff00` rc=0 → `prepared on own render node`（`card2`/`renderD129`）；`run=1` rc=0，8 秒后出 fired 行（逐块 5s 预算未触发，无 fence 行即无等待超时）。
3. `rmmod` rc=0，节点消失，refs 回 bridge 1/probe 1；窗口 WARNING/BUG/Oops 计数 0。
4. 证据 `reports/r284-bigrect-live.dmesg`（0600，6 行 fire 相关）；暂存区已清空。无代码改动、无需门禁重跑（`check-offline` 状态沿用 r283：366+292）。

## 边界

- 本轮只证明分块数学与颜色参数的通用性；单发语义、soak 重复性、UMD  derived 矩形仍未覆盖。
- 矩形上限仍受 `MT_FIRE_MAX_CHUNKS=64` 约束（如 4K 全帧 3840×2160 需 128 块，会 `-E2BIG` 大声拒绝，未试）。

## 下一步（候选）

1. soak 重复性（同模块多轮装/卸，ref 对称性）；或
2. 合并策略 recon（fire 合进 bridge vs 保持独立）。
