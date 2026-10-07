# r247：translator 并发冲突实锤（批准执行）——全局 markers 状态机不支持并发 submit，败者 `-EBUSY`

- **结论**：translator 并发行为在活体探明：ck 桥下双混合进程并行，一胜一败——胜者全过，败者混合 fire **59µs 即时 FAIL errno=16（EBUSY）**，随后 probe 5s 超时（update 从未写回）。全局 markers 状态机（`ready/work_ready` + `submit_context`）不支持并发 submit：败者在 submit 环节被拒（tag 被消费但无 translated 行）。胜者 probe 对称完成；拆桥 `unloaded cleanly`（probe 25→1）；桥恢复默认 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；dmesg 打 `[r247] ck-concurrent-start` 标记；`rmmod`（ref 0）→ `insmod translate_kick=1`（probe 未碰）。
2. 首轮双跑：一胜（45ms 全过）一败（混合 FAIL + probe 5s 超时）；dmesg tag 序列出现跳号（tag=2 缺失），定位到败者拿 tag 但 submit 被拒。
3. 取证轮：给工具加 errno 上报（`bridge_call_logged`，失败即报 errno；无门禁绑定，纯取证不入库——工具门禁只覆盖行为断言，errno 打印是诊断格式），重载 ck 桥后重跑并发，败者 `mixed fire accepted FAIL errno=16` 实锤。`poll` 断言在 fd=-1 路径的假阳性一并发现（`poll(fd=0)` 误报 ok，见边界）。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node/smoke 全绿；终态 1/0；dmesg 计数 0。
5. `check-offline`：327 Python OK（errno 上报无新增断言，门禁数不变）；C/内核沿用 r222（本轮零内核改动，未重跑）。

## 边界

- tag=3/5 归属未完全闭合（成功行与失败日志的对应存在 63s 间隔疑点，如实记录；EBUSY 本体无疑）。
- `fence fd pollable` 在 fire 失败路径下 poll 了 fd 0（假阳性 ok）——工具小 bug，下轮修复（失败即跳过 poll）。
- r237 的 observer 并发（无状态）与本轮对照：observer 可并发，translator 不可——符合设计（全局 translator_lock 串行化），但 submit 环节是硬拒绝而非排队，这是新信息。
- 未用 `timeout` 包裹；无内核改动。

## 下一步（候选，需批准）

- 工具 poll 假阳性修复（离线小改）；TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
