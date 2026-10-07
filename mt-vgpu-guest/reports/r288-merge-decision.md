# r288：合并策略 recon（离线决策）——保持独立 + 桥侧留 UMD 路径，各司其职

- **结论**：fire 不“合进 bridge”也不删除：`mt_live_tqx_fire` 收敛为正式执行验证工具（已验证，不再动）；bridge 内 r280 fire 保留为 UMD 驱动集成路径（默认关闭），但记一处**已证实的潜在缺陷**（流水线 prepare 自阻塞，合流前必须移植串行设计）。二者输入不同（已知矩形 vs UMD pool），不存在重复建设。

## 论证（执行过：只读核查 + 活体证据对照）

1. **模块够不到 UMD 矩形（实测）**：bridge 全文件 `EXPORT_SYMBOL` 计数 = 0；pool PMR 是 bridge per-file 结构，`locate_dst` 依赖的 `file->pmrs` 跨模块不可达。UMD 驱动的 fire（真实绘制）在架构上只能发生在桥内——独立模块永远只能打已知矩形。这是分工的决定性依据，不是偏好。
2. **桥侧 fire 含已证实的潜在缺陷（活体反证）**：r280 桥代码是“全部 prepare+submit 再统一等”（`pvr_submit3_transfer_fire` 循环存 fences → work 验尾块）；r283 首跑实锤：块 1 prepare 即 `-EBUSY`（pool slices 随提交外借、完成才归还）。桥代码从未上机（无重载窗口），故该缺陷仍在盘内潜伏。**合流/启用前必须把 r283 串行流（prepare→submit→等→验逐块）移植回去**，否则首跑即卡块 1。
3. **bridge 重载窗口依然不存在（本轮复核）**：renderD128 仍被重启后的桌面（PID 65770）持有。但解锁程序已验证可行：r279 关 Chrome 即窗口；桌面对应物是 `systemctl --user stop app-...service`（r281 杀进程会触发重启，stop 才行），代价是 UI 消失、agent 后端同命（serve 随应用生命周期）——需用户协调专属窗口，非技术阻塞。
4. **风险不对称**：bridge 是全部 UMD 流量的生产面（freeze 中）；模块是零IRS（insmod/rmmod 只增卸）的验证面。已验证的执行链（3 几何 + 3 颜色 + 5 轮 soak + 64 上限）不应为合并而回炉；合并的唯一收益（UMD 矩形驱动）在窗口打开前无法兑现。

## 决定

- `mt_live_tqx_fire.c`：转正式工具，不再为“合并”改动；后续只接受修 defect 级改动。
- `mt_pvr_bridge.c` r280 fire：保留（`translate_tqx_fire` 默认关），记 defect 待修（串行移植），修复仅随 holder 窗口一起（窗口不到不动桥）。
- 下一个动桥的 live 目标仍是 STATUS #1（真实 DDK2 render 后端），fire 合流是其子项而非前置。

## 边界

- 本轮纯文档：无代码改动，门禁状态沿用 r287（366+292）。
- 缺陷只记在报告 + MEMORY 遗留，未进快照 §5（攒刷新 pass 按 §3 约束）；未在桥源码加注（保持本轮纯文档）。
