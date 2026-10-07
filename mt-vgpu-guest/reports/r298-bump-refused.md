# r298：bump 首跑被拒 `-95`（批准执行）——大声失败证实因果模型

- **结论**：`=2` + bump 窗口（fire 关，隔离变量）：真实 blit 在 submit3 即被拒——`submit3 bump refused: -95`（`-EOPNOTSUPP`，某 update 条目 `pvr_translator_resolve` 无 CPU 可见内存），submit 回错，UMD **即时 134**（无 hanging）。对照成立：hang ⟺ 成功但无完成语义；abort ⟺ 明确错误（r172 `-25` 同构）。因果模型再添一证。拆桥干净 + 默认 + L3 双绿 + 桌面拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动（r297 构建直接上机）。

## 实测（执行过）

1. 批准：standing 授权 + r297 预告。停桌面 → ref 0 → 拆桥 → `=2 + translate_submit3_bump=1`（fire 关）→ 真实 blit（134 即时，无 hanging；8201 行）→ 拆桥 → 默认 → L3 双绿 → 拉桌面。
2. dmesg：observe 行正常（`va/4608/res/pmr` 全同，`nonzero=40` 第 13 轮值 `da 4e`）；随后 `submit3 bump refused: -95`，submit 回错，UMD  abort。trace 末三条：`0x6:0x15/0x6:0x13`（0）→ `0x89:0xa`（-95），有序拆除极简（103 调用）。
3. 归因（执行，非推断）：`-EOPNOTSUPP` 只出自 `pvr_translator_resolve` 的 `!pmr->host || !pmr->bytes` 分支（种类错是 `-ENOENT`，越界是 `-ERANGE`）——某 update 柄指向的 PMR 无 CPU 映射，或柄非 PMR/SYNC 种类。**哪一条、什么柄、什么偏移——拒绝行没打印，下轮补逐项诊断。**
4. 证据：`r298-bump-refused.jsonl`（0600，8201 行）；dmesg 行见上（窗口 `.dmesg` 未单存——失误，教训：窗口 dmesg 落盘应进脚本，本轮只有剪贴行）；暂存区已清空。

## 边界与下一步

- 本轮证伪了“bump 一次写对”——resolve 层先挡住。r299：逐项诊断行（index/handle/offset/ret）+ 重建 + 复验，看是柄种类问题还是 host 缺失（若是后者，备选：对无 host PMR 建立 CPU 映射或只写有 host 者并报数——需 recon，不预设）。
