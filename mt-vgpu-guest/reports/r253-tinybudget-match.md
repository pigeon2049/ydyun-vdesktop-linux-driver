# r253：极小预算匹配腿（批准执行）——100ms 下命中仍通过

- **结论**：`translate_kick=1 translate_wait_ms=100` 下预置匹配（check=2 + update=2）**45ms** 即过，写回 probe 0.10ms 即过；dmesg `check=2 update=2 tag=1 fence=59` → `check=1 update=0 tag=2 fence=60`（fence 序列延续）。45ms 中 prepare 占大头（r241 结论），等待命中本身远小于 100ms 预算。预算下限探索：100ms 仍安全，命中路径不受预算挤压。拆桥 `unloaded cleanly`（probe 25→1）；桥恢复默认 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；dmesg 打 `[r253] tinnybudget-start` 标记（typo 如实保留）。
2. `rmmod`（ref 0）→ `insmod translate_kick=1 translate_wait_ms=100`（probe 未碰，默认 major）。
3. 活体：9 项全 ok，exit 0。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 失配腿在 100ms 下预期 0.1s 超时（未跑；r239 的 1s 腿已证比例关系，外推可信但未验证，如实声明）。
- 未用 `timeout` 包裹；无代码改动。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
