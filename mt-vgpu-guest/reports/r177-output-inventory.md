# r177：T3 输出侧盘点——Transfer 归 TQX，颜色/尺寸不在 CCB 内（零硬件触碰）

- **结论**：输出侧分两条，已有发射能力：DM2 3D 包（模板 + tag + RT 补丁，`mt_translate_kick.h`，check-only 已验证）与 TQX 程序（fill 矩形19-DWORD 构造器 + copy `{src,dst,bytes}` 分块计划，均在树）。关键定向：Transfer（blit）归 TQX 通道，不归 DM2——DM 格式反推只欠 TA/3D kick（S4 另线）。CCB 39B 几乎全是指针/句柄/标志，fill 的颜色与矩形几何**不在其中**（body 全零），必在 surface 状态或 PMR sync 侧——这是 T3-transfer 的首要缺口。VA→PA 翻译机制已有（reservation/PMR 定界），完成语义有 fence  infra。本轮纯只读盘点，无代码改动。

## 输出侧现状（文件级）

1. DM2：`mt_gfx_packet_template.h`（18KB，头字段 + 全零区）+ `mt_translate_kick.h`（tag@0x08、RT 四槽 `0x45a0/0x45a8/0x45b0/0x4668`）。只会发空 marker；颜色不在此包（fill 色走 TQX）。
2. TQX fill：`mt_tqx_fill.h`（`destination_va + rect + inline color[4]` → 19 DWORD；`live_3d_drm.c:455` 调用，r66/r70/r71 活体验证）。
3. TQX copy：`mt_tqx_copy.h`（`{src,dst,bytes}` + chunk/surface/state 描述符计划）。存在，未被 UMD 流量验证。
4. 翻译接线位：`live_3d_drm.c` fill 路径（VA→slot→bo→`mt_tqx_fill_work_prepare`→`submit_tqx_work`→fence）是 T3-transfer 可复用的发射链；CCB 侧需把 payload B 的 VA 字翻译成 `src/dst` 并补齐几何/颜色。

## 缺口清单（按序）

1. **几何/颜色来源**：CCB 内无 → 候选 surface 创建链（Rogue2D surface/layout，r100–r108 卡点史）或 PMR sync 数组；需追踪 transfer 的 surface 对象。
2. **payload B 的 VA 归属**：`0x00a3xxxx/0x00a1xxxx` 不在任何观测 reservation 区间内（`0x8000…/0xda…/0xe0…/0xf0…`）——子分配器偏移或另地址空间，未定。
3. **DM 格式**：仅 TA/3D 需要，Transfer 不需要——STATUS 步骤 3 保持 S4 边界，不与 T3-transfer 混淆。
4. 完成语义：fence infra 现成；update 写回语义离线已定（r159），活体待 `=2` 窗口。

## 边界

- 本轮未执行任何发射、无模块动作；会话未碰（probe ref 1、bridge ref 0，开工收工一致）。门禁复核 `check-offline` 全绿（274+272）。
- “颜色不在 CCB 内”基于单 fill 样本（body 全零）；若他样本 body 非零，本条需修正——已在规约未知项留口。
