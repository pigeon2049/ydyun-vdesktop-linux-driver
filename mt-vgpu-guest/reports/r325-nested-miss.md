# r325：嵌套断点与返值扫描双空跑（批准执行）——GDB 机制边界探明

- **结论**：两次 GDB 运行均执行完整 blit（各即时 abort，无 hang——`=2` 无 bump 的预期形状）但取数双双落空：① 嵌套版（catch-load 内 `break TQ_LookUpEOT` + `commands{finish,printf,cont}`）：断点建成就绪（`Breakpoint 2 at`），命中后 `finish` 未回到可观测状态、无 `EOT-RET` 行、无 signal 行——`finish` 在 batch 嵌套语境静默失败；② 返值扫描版（catch 内 `disass` 找 `ret`）：`RET-SITES: 0`——`LookUpEOT` 内无 `ret` 指令（尾跳风格，与本仓库习惯一致，r321 回响），返值经由跳转链，线性找 `ret` 先天无解。**返值问题不可用断点/dprintf/反汇编三板斧解决**，需换思路（见下一步）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。
- **副产品（有用）**：eotret trace 17290 行 = 两次完整 blit（tid 各 8201）+ 453 行 GDB 自身读（r296 自污染同构）；blit 在 GDB 下行为与裸跑一致（abort，无 hang）。

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → `=2` → 嵌套版跑空 → 返值扫描版跑空（0 rets）→ 读输出定性 → 拆桥 → 默认 → L3 双绿 → 拉桌面（用户中途重开挡回一次 rmmod，二次停后关账）。
2. 证据：`r325-nested.txt` + `r325-nested-attempt.txt`（0600，两版脚本输出）+ `r325-eotret.jsonl`（0600，双 blit + GDB 杂波）；暂存区已清空。门禁沿用（386+299）。

## 边界与下一步

1. 返值新思路（按成本排序）：a) `LookUpEOT` 出参指针（rsi 入参指向的调用者栈结构）在 abort 后 core 里直接读——r317 core 现成，离线可做，零窗口；b) 反汇编改用控制流跟随而非线性扫（`TQ_LookUpEOT` 真入口起单步语义重建，重）；c) 接受返值不可得，转向行为级判据（给不同输入看 abort 有无——如伪造 sync 状态，窗口实验）。
