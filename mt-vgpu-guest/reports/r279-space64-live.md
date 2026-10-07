# r279：64 页绕行验证（批准执行）——slices 成功，hanging 系 UMD 行为

- **结论**：space 64 页绕行在活体证实有效：`=2` + tqx_ctx 新构建（Chrome 已关，ref 0 可重载）上机 + 真实 blit，dmesg 落 `tqx slices: ready cores=1` + `tqx-ctx: ready`——bind_boot_shared 在 64 页下通过，2112 页黑盒绕过。blit 随后 hanging 60s 被外部 timeout 终结（UMD 自身不定行为，r174/r182 先例；无 D 态、可 rmmod，与 r263 真死锁泾渭分明）。拆桥 `unloaded cleanly`（probe 31→1 对称归零）；桥恢复默认 + L3 全绿（node + smoke）；本轮窗口零 WARNING/BUG/Oops（累计 10 行全系 r275 旧转储）。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：Chrome 已关（`ps` 无进程、零持有、bridge ref 0）；`/ 27%`；dmesg 标记 `[r278] space64-start`（开工时编号未定，沿用，见下）；`mkdir build/traces/r278`（新约束流程）。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_tqx_ctx=1`（probe 未碰），节点仍 `renderD128`。
3. 真实 blit（`timeout -s KILL 60` 防护）：observe + slices ready + tqx-ctx ready 全现；UMD 60s 未退被杀（137）。证据 trace 已入库（`r279-space64.jsonl`，9325 行；暂存区已清空）。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0。

## 边界

- slices 内容正确性未验证（只证明 prepare 成功）；fire 分块循环仍缺。
- blit hanging 机制未深究（UMD 侧不定，内核侧健康是判定依据）。
- 未用裸 timeout 包裹 ioctl；本轮无代码改动（r278 离线部分已在盘）。

## 下一步（候选，需批准）

1. fire 分块循环（离线实现+门禁）。
2. 分块 fired/verified 活体（批准执行）。
