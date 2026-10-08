# r323：setup 三元组全返回 + abort 在其下游（批准执行）

- **结论**：GDB 文件脚本 dprintf（非停机）全程记录 setup 期：`TQ_BlitInit`、`TQ_CheckFences`、`TQ_LookUpEOT` **三者全部进入且返回**（参数已记录：`LOOKUPEOT rdi=0x1 rsi=(nil)`），随后 SIGABRT；`RGXTDMSubmit` 按名 dprintf 从未命中——submit 路径根本没走到。abort 点在其下游（`LookUpEOT` 返回之后、submit 之前），与 r322 的尾跳 abort 自洽。GDB 方法论副产品：batch `-ex` 展不开 `commands` 块（r296/r320 定论），脚本文件 + dprintf 是正解（本轮三命中零干预）。拆桥 + 默认 + L3（node 双绿；smoke 在 `=2` 上已绿）+ 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → `=2` → dprintf 版双发（setup 三元组 + RGXTDMSubmit 点名）→ 读命中行 → 拆桥 → 默认 → L3 → 拉桌面（用户中途重开挡回一次 rmmod，二次停后关账）。
2. 证据：`r323-setup-trace.txt`（0600，三命中）+ `r323-setup-trace2.txt`（0600，RGXTDMSubmit 零命中）；暂存区已清空。门禁沿用（386+299）。

## 边界与下一步

1. abort 点在 `LookUpEOT` 返回之后：下一刀是顺藤摸瓜（`LookUpEOT` 返回值/出参是什么？dprintf 只能看入口——返值需断点停机读 `rax`，或静态跟 `0x65200` 返回后的分支）。
