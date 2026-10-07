# r242：DDK2 短预算失配（批准执行）——major×预算双正交

- **结论**：`=2` + `translate_kick=1` + `translate_wait_ms=1000` 下 DDK2 失配 kick（值1 vs PMR 0）**1.008s** 后 `RGXKickSync → 37`——与 legacy 短预算（r239，1.008s）、DDK2 默认预算（r234 Leg2，5.005s）三方同构；等待语义与 major、预算取值双正交。无 marker（dmesg 无新增 translated 行）。拆桥 `unloaded cleanly`（probe ref 自归 1）；同窗口续跑 r243，恢复见 r243。**Freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0；UMD SHA `b3058c02…`；dmesg 打 `[r242] ddk2-shortmismatch-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_kick=1 translate_wait_ms=1000`（probe 未碰），节点仍 `renderD128`。
3. 活体：r234 Leg2 同链（DDK2 全链 `b5*` + 值1 kick），1.008s stall → kick 37，harness 存活退出。证据：`reports/r242-ddk2short.jsonl`（125 行）。
4. 未恢复（同窗口换参数续跑 r243；见 r243 恢复节）。

## 边界

- DDK2 非零预置仍断（r228）；本轮只证明 DDK2 下等待+预算语义真实。
- 未用 `timeout` 包裹；无代码改动。
