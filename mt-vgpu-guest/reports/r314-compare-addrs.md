# r314：比对双方点名（批准执行）——`dest+0` vs `source+3841` 全 5MB

- **结论**：GDB 断比对循环首迭代（`0x4624`，ASLR 基址已先探）：`rcx=0x1019映射+0`、`rsi=0x1032映射+0xF01(3841)`、`r13d=0x500000`(5242880B 整面)。UMD 比对 `dest[0..5242879]` vs `source[3841..3841+5242879]`——目的像素起于池基址（HEAD 不是目的偏移），源像素起于 HEAD。`todst` 拷贝基址据此改为池基（r315）。L3 双绿，refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：standing 授权。零硬件探基址（`start` 停 main，`0x555555554000` 确定性基址）→ 停桌面 → `=2` + bump → GDB 断点版 blit（首迭代停机，batch 退出后 inferior 保留）→ 二次 attach 读寄存器 + 映射（`rcx→0x1019`、`rsi→0x1032` 跨映射对照）→ kill → 拆桥 → 默认 → L3 双绿 → 拉桌面。
2. 第一版 `commands` 写法在 batch 下失效（pending 断点语境），改裸断点 + 事后 attach（r296 同教训延续：batch 里只用最简形式）。
3. 证据：`r314-cmp-regs.txt`（0600，寄存器 + 映射）+ `r314-cmp.jsonl`（0600）+ `r314-cmp-break.txt`（0600，断点命中）；暂存区已清空。无代码改动。
