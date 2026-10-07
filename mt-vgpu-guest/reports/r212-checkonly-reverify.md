# r212：check-only kick 新会话复验通过（批准执行）——legacy `translate_kick=1` 经真实 DM2 空 marker

- **结论**：r211 新会话（trial `20261007T040408Z-f3fb55af`）上，legacy check-only kick 复验通过：桥以 `translate_kick=1` 重载，r73 配方（rung8 链 + `buf 26 512` + `u32@216=1`/`u64@224=*b10`/`u32@232=0`，PASSTHROUGH）六符号全 0、`RGXKickSync → 0`、harness exit 0；`0x88:0x4`（84/8）`ret=0`；dmesg `translated kick: check=1 update=0 tag=1 fence=1`（与 r148 同形）。拆桥 `unloaded cleanly`，probe ref 25→1；桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：`/tmp` 2%；UMD 归档 SHA `b3058c02…` 对版（`/tmp` 副本重启已丢，按 Makefile 同款 `cp` 恢复）；桥 ref 0 可直接 rmmod；dmesg 打 `[r212] checkonly-start` 标记。
2. `rmmod mt_pvr_bridge`（ref 0）→ `insmod … translate_kick=1` rc=0，`renderD128` 重现。probe 全程未碰。
3. check 值直接用 0（r148 教训：新鲜 sync prim PMR 实测值为 0，用 1 会 `-ETIMEDOUT`/UMD 37）：首跑即全绿，无需二次纠偏。
4. 证据：`reports/r212-checkonly-reverify.jsonl`（130 行，已入库；r148/r149 的 `/tmp` 易失 trace 重启即丢，本轮起持久化）。
5. 恢复：`rmmod` → `unloaded cleanly`，probe 25→1，render node 移除；`insmod` 默认桥 → `pvr_node_probe /dev/dri/renderD128` 0 failing/0 mismatch；终态 ref 1/0；dmesg `WARNING|BUG|Oops` 计数 0。

## 边界

- 仅 legacy check-only 空 marker 复验；DDK2（`=2`）check-only（r149 对应项）仍待复验；非零 CCB、update 数组活体、TA/CDM 专属提交均未碰。
- 本轮一次只载一个 live 配置（check 桥），做完即卸并恢复默认；未跑 `make probe`/`make umd`（会先 rmmod），未用 `timeout` 包裹 harness。
- 无代码改动、无需门禁重跑；`make kernel` 状态沿用 r211（W=1 零警告，vermagic 已对版）。

## 下一步（候选，需批准）

- DDK2 check-only 复验（`drm_major=2, translate_kick=1`，r149 配方，`b5*` 链）。
- 真实绘制 CCB 仍待 DDK2 render backend 接线（r207/r208 边界）；不得用 accept-and-log 代替执行。
