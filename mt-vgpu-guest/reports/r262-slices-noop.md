# r262：slices 活体未达预期（批准执行）——零执行零打印，原因待查

- **结论**：r261 slices 的活体验证**未达预期**：`=2` + `translate_tqx_ctx=1` 新构建（build-id `f2bf4f1e` 与盘内一致，反汇编确认调用存在）上机 + 真实 blit 后，`tqx-ctx: ready` 正常出现，但 `tqx slices:` 三种打印（topology/prepare/ready）**全无**。bring-up 其余路径正常（observe + ready + 对称 teardown），translator 功能不受影响。已排除：在载≠盘内（build-id 一致）、调用点错误（源码+反汇编双确认在 tqx 块内）、dmesg 丢失（同窗口其他行齐全）。未能排除：调用未到达（需调用点前后加打印重编验证，留待下轮）。拆桥干净，默认恢复 + L3 全绿。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：盘内 `.ko` 含 slices 字符串（3 行）+ 76 处引用；vermagic 对版；dmesg 打 `[r262] slices-live-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_tqx_ctx=1`（probe 未碰）；`Initialized pvr 2.1.0`。
3. 真实 blit（134 预期内）：observe 行正常（VA/39B，`+0x40` 第七个值 `bb ed`）；`tqx-ctx: ready` 正常；slices 零行。
4. 排查（执行）：build-id 对比一致；`objdump -dr` 确认 prepare 内 `call pvr_translator_tqx_slices`；dmesg 时间窗确认（uptime 11:16，40287 为本轮）；handler ready 条件复核（`tqx_ready` 在 slices 调用之后置位，矛盾成立）。
5. 恢复：`rmmod` → `unloaded cleanly`（probe 25→1）；`insmod` 默认桥 → node 0 failing；终态 1/0；dmesg 无新增 WARN。暂存区已清空（trace 未落盘——`UMD_TRACE` 目录未建，命令笔误，如实记录）。

## 边界

- 未达预期 ≠ 回归：slices 是新增非致命路径，DM/translator 原有语义全绿。
- 本轮未用 `timeout` 包裹；无代码改动（r261 代码已在盘，未重编）。
- 教训：活体验证前应先确认"调用点可达"的最小信号（如调用点前后各一行打印），而不是只依赖函数内打印。

## 下一步（候选，需批准）

1. 调用点前后加打印 + 重载验证（离线小改 + 活体）。
2. fire 函数（离线实现+门禁）。
