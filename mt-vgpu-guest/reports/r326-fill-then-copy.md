# r326：先 fill 后 copy，abort 纹丝不动（批准执行）——EOT/会话态假设证伪

- **结论**：同一窗口（五开）先跑通 blit（`Test PASS`，translator 完成态落袋），紧接着跑 tq-perf：**仍然即时 134，同形 abort**（8206 行/101 全零/末 map@8199）。先完成的 translator 工作**不能**解除 copy 的 abort——EOT/会话完成态假设证伪（至少 translator-marker 完成态不是钥匙）。abort 条件自带（per-context 或输入内禀）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。
- **副产品**：tq-perf trace 8206 行（多 6 行 mmap 系杂波，无意义）；blit PASS 复现一次（translator 火路回归正常）。

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → 五开 → blit（PASS）→ tq-perf（134）→ 读 trace → 拆桥 → 默认 → L3 双绿 → 拉桌面（本轮用户未重开，一次关账）。
2. 证据：`r326-fillthen-copy-blit.jsonl` + `r326-fillthen-copy-tqperf.jsonl`（0600）+ tqperf stdout（0600）；暂存区已清空。门禁沿用（386+299）。

## 边界与下一步

1. copy 中止条件只剩：per-context 内禀（TQ context 状态机）或输入内禀（某 IN 值）。下一刀：GDB 断 `TQ_CheckFences` 看它读什么/返回什么（fence 状态机是最大嫌疑——CheckFences 的名字就写在 abort 上游），或静态跟 `0x65200` 返回分支。
