# r328：CheckFences 字段值 + 返回值被忽略（批准执行）

- **结论**：GDB dprintf（catch-load + 绝对地址，r322 模式复用）抓到 `TQ_CheckFences` 入口全字段，两轮一致：**`type=[rdi+0x3c]=0`、`count=[ctx+0x204]=1`、`flags=[rdx]=0`、`a8=[rdx+0xa8]=0`**。离线对齐反汇编（真入口 `0x65240` 起，可靠）解出调用规约：调用点（`0x6015e`）以 `r8=&local` 传出参，**返回值 `edi` 被直接丢弃**（其后 `mov` 覆盖，无分支）——`[r8]=1` 系出参写入而非 mismatch 标志；调用方继续取 `[r12]`/`-0x27c(%rbp)` 调 `TQ_LookUpEOT`。极性之争消解：CheckFences 经出参通信，abort 判据在其下游状态累积，不在返回值。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。
- **副产品**：`TQ_CheckFences` 全函数对齐解码完成（type switch 1/2/4/5/default + fence 对象 `0x8` 位循环 + `[r8]` 三处写入点 + `r9` 非空写），见分析正文；`RGXTDMSubmit` 按名 dprintf 零命中（静态符号，名不可达——r322 的 minimal-symbol 归因存疑，备忘）。

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → `=2` → dprintf 版两发（首发 `%` 转义漏改二次，备忘：python 串里的 dprintf 格式符须 `%%`）→ 读字段值 → 拆桥 → 默认 → L3 双绿 → 拉桌面（本轮用户未重开，一次关账）。
2. 证据：`r328-cf-fields.txt`（0600，二轮一致值）+ `r328-cf.jsonl`（0600）；暂存区已清空。门禁沿用（386+299）。

## 边界与下一步

1. 下一刀：`[r8]` 出参槽（调用者栈 `-0xe3c`）在 abort 前的值——core 里读（r317 core 现成，离线），或 GDB 断 abort 桩读调用者帧局部（r322 配方复用）。出参值 = CheckFences 的“判决书”。
