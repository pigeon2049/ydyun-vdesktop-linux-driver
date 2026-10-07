# r320：abort 桩活体机制 + 栈取证（批准执行）——止于验证，结论已足

- **结论**：GDB mechanics 精炼：`start` 在 stripped 主程序上无 `main` 符号（失败）；绝对地址断点需 python 现算基址（`info proc mappings` + VMA 偏移，本轮验证可用：`Breakpoint 1 at 0x7ffff742c8c0`）。abort 点寄存器已破坏（`raise` 参数覆盖：rdi=rsi=pid，rdx=6）——活体读参此路不通，备忘。离线栈取证（r317 core）得手：TQJobSubmit 栈上有 **destination-magic u64（`0x002da10040000005`）+ `0x3ff` 维度对**——abort 发生在组装 surface 描述符期间（map 之后、提交之前），与“101 全零后无 bridge 流量”一致。copy 缺的是 UMD setup 内部校验，不是桥调用。
- **恢复**：本轮窗口含两次 GDB 符号折腾，无多余 hang（tq-perf 即时 abort 特性反而让窗口短）；拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → `=2` → tq-perf 走 GDB（abort 点寄存器已破坏，记录）→ 绝对地址桩断点验证可用 → 拆桥 → 默认 → L3 双绿 → 拉桌面（用户中途重开挡回一次，二次停后关账）。
2. 证据：`r320-abort-regs.txt`（0600，破坏现场）+ `r320-stub-attempt.txt`（0600，batch 符号教训）；core 栈值见上（r317 core 已入库，可复现）。暂存区已清空。门禁沿用（386+299）。

## 边界与下一步

- copy/TA 均为深水 RE + 新翻译代码 + 多窗口工程；transfer-fill 之外无捷径。提议：本轮收官，push + 休息；大项另立（TA producer recon 或 copy 全反汇编，二选一，需用户拍板——不再默认烧窗口）。
