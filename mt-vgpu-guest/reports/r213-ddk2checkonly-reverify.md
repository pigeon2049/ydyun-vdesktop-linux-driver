# r213：DDK2 check-only 新会话复验通过（批准执行）——`=2` + `translate_kick=1` 经真实 DM2 空 marker

- **结论**：r211 新会话上 DDK2 check-only 复验通过：桥以 `drm_major=2 translate_kick=1` 重载（probe 未碰），r149 配方（r145/r146 全链参数 + r73 check 注入，check 值 0）六符号全 0、harness exit 0；轨迹命中 `0x82:0x12`（12/12）、`0x88:0x5`（8/12）、`0x88:0x4`（84/8），均 `ret=0`；dmesg `translated kick: check=1 update=0 tag=1 fence=2`（与 r149 逐字同形）。拆桥 `unloaded cleanly`，probe ref 25→1；桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：`/tmp` 2%；`/tmp` UMD 副本 SHA `b3058c02…` 仍对版（r212 恢复后无重启）；桥 ref 0 可直接 rmmod；dmesg 打 `[r213] ddk2-checkonly-start` 标记。
2. 首跑沿用 r212 legacy 链形（CCB create 传 `b5`），UMD 在 `RGXCreateKickSyncContextCCB` 后用户态 SIGSEGV（exit 139）——正是 r149 记载的“漏掉 `b5*` 间接”形态，也是 r144 的定论（DDK2 要 `b5[0]`，legacy 传 `&b5` 是巧合能跑）。桥侧干净回收（arena close 两行，refs 1/0，无内核异常）。
3. 仅改 CCB 一个传参 `b5`→`b5*` 后重跑：六符号全 0，exit 0。证据：`reports/r213-ddk2checkonly-reverify.jsonl`（249 行，已入库）。
4. 恢复：`rmmod` → `unloaded cleanly`，probe 25→1，render node 移除；`insmod` 默认桥 → `pvr_node_probe` 0 failing/0 mismatch；终态 ref 1/0；dmesg `WARNING|BUG|Oops` 计数 0。

## 边界

- 仅 DDK2 check-only 空 marker 复验；`0x82:0xC` TA / `0x81:0x5` CDM、非零 CCB、update 数组活体均未碰。
- 本轮一次只载一个 live 配置，做完即卸并恢复默认；未跑 `make probe`/`make umd`，未用 `timeout` 包裹 harness。
- 无代码改动、无需门禁重跑；`make kernel` 状态沿用 r211。

## 下一步（候选，需批准）

- 真实绘制 CCB 仍待 DDK2 render backend 接线（r207/r208 边界）；不得用 accept-and-log 代替执行。
- 同步 update 语义活体验证（STATUS #2）：r159 离线结论 + r203 fabricated `flag=2`，待真实 `0x82:0x14` handler。
