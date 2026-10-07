# r266：slices 重验通过（批准执行）——锁序修复生效，`tqx slices: ready`

- **结论**：r265 锁序修复在活体证实生效：`=2` + `translate_tqx_ctx=1` 新构建上机 + 真实 blit，dmesg 落 `tqx slices: ready cores=1`（bring-up 调用点三行 + slices ready 全现，无死锁、无 D 态）。blit 随后 hanging 60s 被外部 timeout 终结（UMD 自身行为，r174 先例：首轮 exit 0 / 次轮 hanging 不定；本次无 D 态、可 rmmod，与 r263 真死锁泾渭分明）。拆桥 `unloaded cleanly`，probe ref 30→1（-29 对称归零，translator/bridge 持有全放，无泄漏）。桥恢复默认 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；盘内 `.ko` 含 slices 字符串（4 行）；vermagic 对版；dmesg 打 `[r266] slices-retry-start` 标记；`mkdir build/traces/r266`（新约束流程）。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_tqx_ctx=1`（probe 未碰，装盘前验 strings + vermagic）。
3. 真实 blit（`timeout -s KILL 60` 防护，r182 先例）：`enter → before → slices: ready cores=1 → after → tqx-ctx: ready` 全现；UMD 60s 未退被杀（137）。证据 trace 已入库（`r266-slices-ready.jsonl`，9314 行；暂存区已清空）。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- slices 就绪 ≠ 可发射：fire 函数仍缺；pool slices 内容正确性未验证（只证明 prepare 成功）。
- blit hanging 机制未深究（UMD 侧不定行为，r174 口径延续；内核侧健康是判定依据）。
- 本轮未用裸 `timeout` 包裹 ioctl（进程级防护，r158/r182 先例）；无代码改动（r265 代码已在盘）。

## 下一步（候选，需批准）

1. fire 函数（离线实现+门禁）。
2. 活体 fired/verified（批准执行）。
