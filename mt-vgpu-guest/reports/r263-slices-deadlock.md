# r263：slices 死锁 + 待重启（批准执行）——`buffers->lock` 自死锁，D 态需重启恢复

- **结论**：调用点打印证实 slices 可达（`enter → before → after` 首轮完整），但第二轮卡死在 `mutex_lock(d->shared_boot.buffers->lock)`（进程栈 + sysrq 双实锤，D 态 blit）。根因：bring-up 持 trial_lock 取 buffers->lock，与某持 buffers->lock 取 trial_lock 的路径 AB-BA 死锁（r67 教训重演；r147 锁序纪律被违反）。首轮零 slices 打印但返回（topology/workspace/prepare/cancel/ready 行全无，原因未明，如实记录）。现状：blit D 态杀不掉 + 桥 ref 1 + probe ref 44 → `rmmod` 被拒，**需重启恢复（待用户批准）**。本轮其余验证（调用可达 + 打印 plumbing）成立。

## 实测（执行过）

1. 离线：调用点前后加打印（enter/before/after）+ `make kernel` 零警告 + slices 门禁 OK；会话未碰（`lsmod` 1/0）。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_tqx_ctx=1`（probe 未碰）；`strings` 确认新打印在盘内 `.ko`。
3. 真实 blit（134 预期内）：dmesg `enter → before → after`（41298，第一轮）+ `enter → before`（41581，第二轮卡住）。
4. 诊断（执行）：`/proc/63979/stack` = `pvr_translator_tqx_slices+0x17d`；`objdump -dr` 定 +0x17d 为 `mutex_lock` PLT；sysrq-w  hung task 确认；单 D 态进程（自死锁形态）；`rmmod` → `ERROR: Module in use`。
5. 未恢复：桥在载（新构建，含死锁代码）、默认未恢复、L3 未跑。**freeze 名存实亡——待重启重建，须用户批准。**

## 边界

- 首轮零打印之谜未解（调用到达但函数内三处打印全无；已排除构建/调用点/dmesg 丢；内存踩踏嫌疑未证实）。
- 教训升级：新锁（buffers->lock）引入 translator 路径必须先做锁序审计（r67/r147 前车之鉴，本轮违反）；`translate_tqx_ctx` bring-up 的扩展须经锁序门禁（下轮补门禁：buffers->lock 获取顺序断言）。
- 本轮未用 `timeout` 包裹 blit（hang 到 150s 工具超时；r67 红线"超时即停手"遵守——未堆任务）。

## 下一步（候选，需批准）

1. **重启 + 会话重建**（r211 先例；需明确批准）。
2. 锁序修复（离线）：slices 的 prepare 移出 trial_lock（先放锁再取 buffers 锁），或 buffers 锁内化；补锁序门禁。
3. fire 函数暂缓（slices 未就绪）。
