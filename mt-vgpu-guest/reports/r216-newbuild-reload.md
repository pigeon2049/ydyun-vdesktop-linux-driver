# r216：r215 新构建上机 + L3 复绿（批准执行）

- **结论**：含 `0x82:0x14` observer 的新构建已上机：`rmmod`（ref 0，probe 未碰）→ 装盘前验 `strings` 含 `kickta3d5 observe` 且 vermagic 对版 → `insmod` 默认参数 rc=0，节点仍 `renderD128`；L3 全绿（node 0 failing/0 mismatch，dma smoke PASS refs 平衡，无 GPU 提交）；终态 ref 1/0；dmesg 仅 arena/DMA/VM plan 信息行，零 WARNING/BUG/Oops。**Freeze 已恢复。**observer 已在载但尚无真实流量（回 0/拒绝语义待 GFX producer 落定后验证）。

## 实测（执行过）

1. 开工预检：同内核 `6.12.111`（uptime 7:03，无重启）；refs 1/0；`/tmp` 2%；dmesg 历史 WARNING/BUG/Oops 计数 0；打 `[r216] newbuild-reload-start` 标记。
2. 重载：单桥操作，probe 会话零触碰；`Initialized` 后节点重现。回滚位：旧构建不可 bit 复现（r120 定论），但本轮增量纯加法（1 case + 1 函数 + 1 宏，既有路径零改行），`git` 可随时重编旧版。
3. L3：`pvr_node_probe /dev/dri/renderD128` 0 failing/0 mismatch；`pvr_dma_smoke` PASS（session refs 1→2→1）。
4. 未做：observer 分发活体 ping（需 raw `0x82:0x14` 发送器，属新工具代码，留待下轮）；无 UMD 流量、无 translator 操作、无 GPU 工作。

## 边界

- 在载桥 = r215 构建（含 observer）；“observer 工作”仍未验证—— cargo 是 parked，不是 proven。
- 无代码改动、无需门禁重跑（`make kernel` 状态沿用 r215）。

## 下一步（候选，需批准）

- observer 路由活体 ping：fresh file 发 108B `0x82:0x14`，预期 `-ENOENT`（无 render context）而非 `-ENOTTY`，证明分发到达；需写最小 raw 发送器（离线先行）。
- 真实 GFX producer recon（离线）；真实绘制执行（待 backend 接线）。
