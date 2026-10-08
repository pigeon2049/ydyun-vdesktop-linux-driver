# r324：LookUpEOT 返回值抓取未遂（批准执行）——工具链教训，证据保全

- **结论**：`finish` 版文件脚本未取到返回值：`break TQ_LookUpEOT` 报“Function not defined”（静态符号，pending 后未命中），后续 `finish/printf/cont` 在无 inferor 上下文静默跳过（9 行输出，无 `LOOKUPEOT-RET`），进程莫名停于 `TQJobSubmit+0x278` 后被 `kill`。r323 的“全进入全返回”表述**收敛为“全进入”（dprintf 入口实锤），返回未证**——返回是否发生仍是开放问题（abort 帧证明调用者已不在栈上，间接支持已返回，但非直接证据）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。
- **工具链教训（定论）**：GDB batch 可靠子集 = `dprintf`（符号）+ 绝对地址断点（python 现算基址）+ 事后 attach；`start`（要 `main` 符号）、batch `-ex` 展 `commands` 块、`finish`（无停机上下文）均不可靠。返值读取的正解：绝对地址断在其**返回点**（`LookUpEOT` 真入口对齐反汇编找 `ret`，或 `finish` 在**已停机**上下文中——即先裸断点停住，再第二会话 `finish`）。

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → `=2` → finish 版脚本跑空 → 读 9 行输出定性 → 拆桥 → 默认 → L3 双绿 → 拉桌面（用户中途重开挡回一次，二次停后关账）。
2. 证据：`r324-lookupret-attempt.txt`（0600，9 行失败现场）；暂存区已清空。门禁沿用（386+299）。

## 边界与下一步

1. r325（短窗口）：裸断点（绝对地址，pending/dprintf 已验证可达）停住 `LookUpEOT` 入口后，**保持停机**，第二 GDB 会话 attach 做 `finish` + 读 `rax`——两步走，绕开 batch 上下文陷阱。
