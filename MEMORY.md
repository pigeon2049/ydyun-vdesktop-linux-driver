# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **总入口见 [`STATUS.md`](STATUS.md)，权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](memory/MEMORY-HISTORY-2026-10-01.md)、
> [`MEMORY-HISTORY-2026-10-03.md`](memory/MEMORY-HISTORY-2026-10-03.md)、
> [`MEMORY-HISTORY-2026-10-04.md`](memory/MEMORY-HISTORY-2026-10-04.md)（只读）。
> 2026-10-05 起归档于 [`MEMORY-HISTORY-2026-10-05.md`](memory/MEMORY-HISTORY-2026-10-05.md)。
> 2026-10-06 起归档于 [`MEMORY-HISTORY-2026-10-06.md`](memory/MEMORY-HISTORY-2026-10-06.md)。
> 2026-10-07 起归档于 [`MEMORY-HISTORY-2026-10-07.md`](memory/MEMORY-HISTORY-2026-10-07.md)。
> 2026-10-08 起归档于 [`MEMORY-HISTORY-2026-10-08.md`](memory/MEMORY-HISTORY-2026-10-08.md)。
> r363 轮按 §4 清理：r361 节已移入归档。
> r364 轮按 §4 清理：r362 节已移入归档。
> r365 轮按 §4 清理：r363 节已移入归档。
> r368 轮按 §4 清理：r364 节已移入归档。
> r369 轮按 §4 清理：r365 节已移入归档。
> r370 轮按 §4 清理：r366 节已移入归档。
> r372 轮按 §4 清理：r367 节已移入归档。
> r373 轮按 §4 清理：r368 节已移入归档。
> r378 轮按 §4 清理：r376 节已移入归档。
> r374 轮按 §4 清理：r369 节已移入归档。
> r379 轮按 §4 清理：r377 节已移入归档。
> r380 轮按 §4 清理：r378 节已移入归档。
> r382 轮按 §4 清理：r379 节已移入归档。
> r383 轮按 §4 清理：r380 节已移入归档。
> r384 轮按 §4 清理：r381 节已移入归档。
> r385 轮按 §4 清理：r382 节已移入归档。
> r386 轮按 §4 清理：r385 节已移入归档。
> r387 轮按 §4 清理：r383 节已移入归档。
> r388 轮按 §4 清理：r386、r384 节已移入归档。
> r390 轮按 §4 清理：r387 节已移入归档。
> r391 轮按 §4 清理：r388 节已移入归档。
> r392 轮按 §4 清理：r389 节已移入归档。
> r393 轮按 §4 清理：r390 节已移入归档。
> r394 轮按 §4 清理：r391、r392 节已移入归档。
> r395 轮按 §4 清理：r393 节已移入归档。
> r396 轮按 §4 清理：r394 节已移入归档。
> 状态冲突时裁决顺序：`STATUS.md` → 快照 → 本文件。






## r397 (2026-10-09): render_ctx 双执行上下文落地，kick 传真实 ctx（活体验证）

**实现**：`mt_render_context.h` 中 `exec_ctx`→`exec_ctx_3d` 改名，新增 `exec_ctx_ta`
（node_type=2→DM3）+ `exec_ta_ready`（struct 1544B→1616B）；create 第 7b 步建 TA ctx
（失败走统一 rollback）；destroy 先 TA 后 3D（顺序无关）；kick 有-context 传
`&rctx->exec_ctx_ta`（file->lock 下借用，marker 不拿所有权故行为中性），无-context
保留 throwaway + 条件 kfree。

**活体**（一次 bridge 重载，safe_rmmod.sh）：V1 双 ctx 创建成功
（`r397: exec process/contexts created (3D node_type=5, TA node_type=2)`）；marker 回归
KICK[ctx]→fence=6（`kick with real exec_ctx_ta (dm=3)`）、KICK[no-ctx]→fence=7，
OUT 与 wire 精确匹配；V3 双 context（0x1000/0x1001）各 kick→fence 8/9，
独立 VM 正确路由，文件关闭 ref 25→13 无泄漏；dmesg 零 WARN/BUG/Oops。

**门禁**：474+299 全绿（T2 新增 dual dm pinning：exec_ctx_ta node_type=2→DM3、
exec_ctx_3d node_type=5→DM2；反向验证通过）；kernel W=1 零警告。

**诚实边界**：marker 级；`exec_ctx_ta` 仅过门禁，未提交真实负载（Phase 3 远期）；
node_type=2 固件语义、in-flight destroy 待验证。

报告 reports/r397-dual-exec-ctx-live-verified.md。

## r396 (2026-10-08): exec_ctx 接入 kick 路径设计——render_ctx 双执行上下文（离线）

**背景**：r391 诚实边界——TA marker 的 work->context 仍是 kick 内 kzalloc 的一次性 dm 标签（仅 route.dm=3），render_ctx 的 exec_ctx（node_type=5→DM2）只贡献了 VM，未参与提交。

**设计**：render_ctx 新增 exec_ctx_ta（node_type=2→DM3），与既有 exec_ctx_3d（改名）共享同一 process（同一 VM/11 BO）；TA kick 传真实 exec_ctx_ta 替代 throwaway（门禁 route.dm==3 通过，r394 T2 白名单天然一致）；3D kick（门控关闭）用 exec_ctx_3d。三阶段：加字段→替换（marker 行为中性，m->context 永不赋值）→真实 payload（远期，需 MT_TA_VM_READY）。TA 仍走 submit_ta_work（0x100 匹配器/sync 回写/D8 拒收皆在其中），完成双路径已处理 m->context。

**待验证**：node_type=2 固件语义；in-flight destroy（-EBUSY 诚实失败 vs 排空）；CSW 的 TA/3D 共用。

报告 reports/r396-exec-ctx-kick-design.md。

## r395 (2026-10-08): 安全网首个实战检验——pre-live 门禁全绿 + TA 回归双 kick 通过（活体）

**背景**：r394 落地的 T1/T2/T3 安全测试首次作为 pre-live 门禁执行。用户要求真机调试前必须跑安全门禁。

**门禁**：check-offline 472+299 全绿（含 9 个新安全测试）；scripts/safe_rmmod.sh 存在且可执行（0755，refcount 守卫）；T1（VM 完整性）/T2（opcode 白名单，(3,0x66) 钉死 DM3）/T3（卸载安全）全部通过；pre-live 清单 5 条逐项核对。

**活体**：两次真实 0x82:0xC TA-only kick（r377 既证 harness：INIT module=2 + bridge 0xc0206440，IN kick_ta=1@188）。ioctl 均返回 0，OUT.error=0，OUT.update_fence=4/5 与 dmesg submitted wire=4/5 精确匹配；零 "completion timeout"（0x100 完成事件到达，走 mt_marker_complete_ta 正常退休）；dmesg 零 WARN/BUG/Oops；refs 不变（bridge 0 / probe 13 基线）。

**安全合规**：本轮零模块操作（未重载、未卸载、未重启）；rmmod -f 零出现；timeout 未使用；两次 kick 串行。

**诚实边界**：marker 级 TA 路径（零绘制）；两次 kick 均走 per-file 回退（ctx=0x0），per-context 路径已在 r391 V3 验证；probe ref=13 基线含 r389 旧泄漏，本轮 delta 为 0。

报告 reports/r395-safety-net-first-live-test.md，证据 reports/r395-dmesg-ta-regression.txt。
