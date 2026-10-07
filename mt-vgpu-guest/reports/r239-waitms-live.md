# r239：等待预算参数语义验证（批准执行）——`translate_wait_ms=1000` 下 1.008s 超时

- **结论**：`translate_wait_ms` 预算参数在活体证实真实生效：桥以 `translate_kick=1 translate_wait_ms=1000` 重载（probe 未碰），失配 kick（值1 vs PMR 0）**1.008s** 后 `RGXKickSync → 37`——与默认 5s 预算的 5.007s（r222 Leg2）同构，超时与预算成比例。无 marker（dmesg 无新增 translated 行）。拆桥 `unloaded cleanly`（probe ref 自归 1）；桥恢复默认 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；UMD SHA `b3058c02…`；dmesg 打 `[r239] waitms-start` 标记。
2. `rmmod`（ref 0）→ `insmod translate_kick=1 translate_wait_ms=1000`（probe 未碰），节点仍 `renderD128`。
3. 活体：r221 失配链形，1.008s stall → kick 37，harness 存活退出。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 只证明预算参数生效；预算内命中路径由 r222 Leg1 覆盖。
- 未用 `timeout` 包裹 harness（桥预算覆盖）；无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
