# r254：极小预算失配腿（批准执行）——100ms 下 0.118s 超时

- **结论**：`translate_kick=1 translate_wait_ms=100` 下失配 kick（值1 vs PMR 0，UMD 链）**0.118s** 后 `RGXKickSync → 37`（100ms 预算 + ~18ms harness 开销）；无 marker（dmesg 无新增 translated 行）。预算维度至此全覆盖：5s→5.007s（r222）、1s→1.008s（r239）、100ms→0.118s，同构。拆桥见 r255（同窗口续跑 soak 后统一恢复）。**Freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0；UMD SHA `b3058c02…`；dmesg 打 `[r254] tinybudget-mismatch-start` 标记。
2. `rmmod`（ref 0）→ `insmod translate_kick=1 translate_wait_ms=100`（probe 未碰，默认 major）。
3. 活体：r221 失配链形，0.118s stall → kick 37，harness 存活退出。证据：`reports/r254-tinymismatch.jsonl`（130 行）。
4. 未恢复（同窗口续跑 soak，见 r255 恢复节）。

## 边界

- 未用 `timeout` 包裹；无代码改动。
