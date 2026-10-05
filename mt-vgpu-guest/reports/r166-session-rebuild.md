# r166：活体会話重建完成（批准执行）——Guest/FW 2/2 pinned，桥默认加载，L3 全绿

- **结论**：`cold_disconnect finish=0/1`（rings idle，`guest=0 firmware=1`，双 clean rmmod）→ `fresh-trial.py --run --runtime-context`（rc=0）→ `mt_guest_probe` 绑定 `00:0e.0`（trial `20261005T161706Z-cf0d876e`，`guest=2 firmware=2`，pinned=1，ref=1）→ `mt_pvr_bridge.ko` 默认参数加载（`card1`/`renderD128`）→ L3 全绿：`pvr_node_probe` 0 failing/0 mismatch（含未知 ioctl 拒绝项），`pvr_dma_smoke` PASS（session refs 1→2→1，无 GPU 工作提交）。dmesg 无新增 WARN/BUG/Oops（r150 的 NULL deref 未复现）。**Freeze 即刻生效。**

## 执行序列（实测）

1. 预检：`6.12.111` 运行核与两 `.ko` vermagic 一致；`make kernel` W=1 零警告（树内即最新构建，未过期）；`00:0e.0` 无绑定；`/tmp` 1%；dmesg 打 `[r166] rebuild-start` 标记。重启后约 1h（uptime 1:08），干净启动。
2. `cold_disconnect finish=0`：`all rings idle, FW=1, result=0` → rmmod rc=0。
3. `cold_disconnect finish=1`：`guest=0 firmware=1, result=0` → rmmod rc=0。
4. `fresh-trial.py --run --runtime-context` rc=0：firmware sha `35d40f75…`（与 r138 一致），`connected=1/published=1/pinned=1`，`guest=2 firmware=2`。
5. `insmod mt_pvr_bridge.ko`（默认参数：`translate_kick` off，`drm_major`/`ddk_feature_set` 默认）rc=0：`[drm] Initialized pvr 0.1.0 ... on minor 1`，`card1`/`renderD128`。
6. L3（注：探针需 `/dev/dri/` 全路径传参，裸 `renderD128` 会 `ENOENT`——harness 用法，非驱动问题）：
   - `pvr_node_probe /dev/dri/renderD128`：`OK: 0 failing step(s), 0 value mismatch(es)`。
   - `pvr_dma_smoke /dev/dri/renderD128`：`PASS`，refs 平衡，无 GPU 提交。
7. 终态：probe ref 1，bridge ref 0，`/dev/dri` = `card0`（QXL）+ `card1`/`renderD128`（桥）；dmesg 仅启动期 CPU 漏洞通告，无模块相关 WARN。

## 边界

- 本轮只做 L3（节点探针 + DMA smoke），未跑 L4、无 UMD 流量、无 GPU 工作提交；`translate_kick` 保持 off。
- r150 Oops 未复现，但根因仍未命名——后续加载 live 实验模块仍一次一个、做完即卸。
- **Freeze**：不 rmmod、不 unbind、不提交额外工作；`make probe`/`make umd`（会先 rmmod）继续禁用。

## 下一步（候选，需批准）

- L4 八级阶梯（真实 UMD，legacy 先行）或 DDK2 `=2` 全链复验；update 数组活体语义（r159 离线结论的活体验证）。
