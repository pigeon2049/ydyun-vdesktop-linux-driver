# r169：OUT 字节级对比——桥彻底无罪；连否 6 个假设，GDB 遮罩仍无解释（批准执行）

- **结论**：`UMD_DUMP_BRIDGE` 全量捕获 10 组桥命令 OUT，对比失败轮（standalone）与通过轮（GDB）：18 处差异**全部**是调用方栈指针/注解指针的回显（`0x6:0x20` 的 nameptr、`0x6:0x9` 的 annotation ptr），其余字节（含堆基址/长度、PMR 句柄、mapping 句柄）逐字节一致——桥在字节级彻底无罪。本轮另证伪 argv[0] 长度（绝对路径仍 3/3 崩）与重试策略（同配方连崩 6 次，rung5 standalone 累计 0/20）。GDB 下 5/5 通过的遮罩机制仍无解释。会话健康，freeze 继续。

## 实测

1. dump 覆盖：`UMD_DUMP_BRIDGE=0x6:0x9,0x11,0x13,0x15,0x20,0xf,0x1e,0x2:0x0,0x82:0x8,0x1:0xc`；失败 55 calls vs 通过 89 calls，前缀 55 中 18 处差异，定位后全为指针回显（见上）。失败 trace 存 [`r169-out-diff-fail.jsonl`](r169-out-diff-fail.jsonl)（通过轮与 r168 的 `l4-tid-pass` 同形，不另存）。
2. 证伪 ledger（累计）：工作线程交错（r168 tid）✗；ASLR ✗；单 CPU ✗；fresh-heap 填充 ✗；argv[0]/栈布局 ✗；重试 ✗。未倒假设：GDB 监督本身（ptrace/启动时序）——尚未找到对照实验。
3. 会话健康：probe ref 1，bridge ref 0，桥错误计数 0，无 GPU 提交。

## 边界

- “字节级无罪”限 dump 覆盖的 10 组命令；`0x82:0x8` 等只在通过轮出现（失败轮死在其前），无法对比——但失败轮死因是 UMD 侧 NULL，与这些调用无关。
- rung6–8 仍被阻；`translate_kick` 仍 off。

## 下一步（候选，需权衡）

- GDB 监督下跑梯（`gdb -batch -ex run`，零断点）：同系统调用、同桥，只是换监督者；5/5 通过率可解 rung6–8 与 update 活体验证。非常规但诚实——报告中如实注明监督方式。
- 或继续猎 GDB 遮罩机制（对照难找，性价比低）。
