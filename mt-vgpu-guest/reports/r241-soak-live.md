# r241：10 轮混合 soak（批准执行）——translator 常驻复用，耗时差定位到 prepare

- **结论**：同窗口（`translate_wait_ms=1000` + `translate_kick=1`，未重载）10 轮混合 fire **10/10 通过**，耗时 0.10–0.20ms/轮——比单发 36–45ms 快约 300 倍。定位：translator prepare（含 address space 创建/上传/seal）在首轮后常驻复用（`if (translator.ready) return 0`），后续轮只剩等待+marker+fence。dmesg 20 行 translated 全落（tag=3..22、fence=20..39，跨 r240 连续无跳号）；零 WARNING/BUG/Oops。拆桥 `unloaded cleanly`（probe 25→1）；桥恢复默认 + L3 全绿。**Freeze 已恢复。**

## 实测（执行过）

1. dmesg 打 `[r241] soak-start` 标记；同桥配置循环 10 次工具，10/10 exit 0。
2. 耗时分布（ns）：102627–197500，中位 ~0.11ms；R_K 单发 36.6ms（含 prepare）。
3. `check=2 update=2` 行累计 13 条（r233/r236/r240 各 1 + soak 10），全 `ret` 隐含 0（translated 行只在成功提交后打印）。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 只证明 translator 复用语义与稳定性；soak 未覆盖 UMD 链（raw 工具）。
- 未用 `timeout` 包裹；无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
