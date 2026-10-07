# r260：ck 下 legacy blit（批准执行）——开关不改变 legacy 路径行为

- **结论**：`translate_kick=1`（默认 major=0，legacy）下真实 `musa_blit_test -device 0 -f -o` 与 r244（默认桥）同形：UMD 走 legacy 路径，止于 `0x89:0x0`（40/12）→ `-25`，随后 SIGABRT（134），从未发出 `0x89:0xa`（trace 8399 行无 submit3；dmesg 无新增 observe 行——此前看到的是 r226/r230 旧行，已用时间戳排除）。ck 开关只影响 `0x88` 翻译，不干扰 `0x89` 路径（预期内，本轮实证）。trace 落硬盘暂存区后已清空（与 r244 入库版同构，不重复入库）。拆桥 `unloaded cleanly`（probe ref 自归 1）；桥恢复默认 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`mkdir build/traces/r260`（新约束流程）；dmesg 打 `[r260] ck-blit-start` 标记。
2. `rmmod`（ref 0）→ `insmod translate_kick=1`（probe 未碰），节点仍 `renderD128`。
3. 活体：blit 自枚举节点；`0x89:0x0` 1 次 `-25`，`0x89:0x5`/`0x89:0x6` 建销正常；UMD 134 系其自身路径。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 本轮是开关无干扰验证，无新语义；`0x89:0x4` 仍无 handler（S4 边界）。
- 未用 `timeout` 包裹；无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- GDB 确认 kick 字段偏移 + fabricated GFX 重建（离线）；bring-up 补 slices（离线实现）；TQX 真发射；CCB 解读；backend 接线。
