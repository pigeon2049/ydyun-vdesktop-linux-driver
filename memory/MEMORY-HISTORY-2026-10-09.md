# MEMORY-HISTORY-2026-10-09（只读归档；原样移入，不回改）

## r400 (2026-10-09): 代码目录重构完成（离线）

**重构**：Phase A 清理 kernel/ 三目录构建产物（gitignored）；Phase B tests 重组为 c/pvr/ta/guest/render/misc 子包（130 文件 git mv，修复 parents/include/Makefile 路径）；Phase C（80 头文件）评估暂缓；Phase D 更新 README 目录表。

**门禁**：474+299 全绿；make kernel W=1 零警告（28 模块）。

报告 mt-vgpu-guest/reports/r400-code-restructure.md。
---

## r399 (2026-10-09): R5 Phase 2 活体验证--无 ctx kick 返回 -EINVAL（活体）

**验证**：pre-live 门禁 T1/T2/T3 全绿后，bridge 重载到 r398 构建
（safe_rmmod.sh，probe 不动）。V1：无 ctx 的 0x82:0xC kick 返回 -EINVAL
（errno 22，dmesg 有 r398 Phase 2 标记）；V2：0x82:0x12 create
（handle=0x1000）后带 ctx kick 成功，OUT.update_fence=10 对应 dmesg
wire=10 精确匹配；V3：destroy 后 probe ref 13->25->13，delta 归零无泄漏。

**门禁**：474+299 全绿；make kernel W=1 零警告；dmesg 零 WARN/BUG/Oops。
本轮无代码改动（纯活体验证）。

报告 reports/r399-perfile-removed-live-verified.md。
---

