# r167：L4 阶梯部分通过（rung1–3 全绿），rung5 卡在 UMD 侧 NULL 解引用（批准执行）

- **结论**：新会话上手动逐级跑 L4（未用 `make umd`，无模块重载）：rung1 connect、rung2 device、rung3 devmemctx 全绿（逐调用 ret=0）。rung4 render 先 2 次 SIGSEGV 后 4 次通过（含 GDB 下 1 次）；rung5 syncprim 在 standalone 下 11/11 SIGSEGV、GDB 下 2/2 通过。core 验尸：`RGXCreateRenderContextCCB+1525` 处 `mov 0x8(%rax),%rdi`（rax=0，即 UMD 对象 `r12+8` 为 NULL）。148 条 trace 逐调用 ret=0、内核零错误、probe ref 稳 1——桥无罪；崩溃是 UMD 侧时序敏感空指针，确切发布者未命名。会话健康，freeze 继续。

## 实测

1. 预检：`/tmp` UMD 在重启后清空，按 Makefile 配方从树内留档 `cp -p` 恢复，SHA `b3058c02…` 一致；`WITH_BRIDGE` 会先 rmmod，全程手跑、桥零重载。
2. rung1–3：connect→0、CreateDevice→0、CreateDevMemCtx→0，exit 均为 0。
3. rung4：初 2 次 exit=139，随后 standalone 3 次 + GDB 1 次 exit=0（同一配方同一桥，GDB 1 次正常走完 render）。
4. rung5（SYNC 配方）：standalone 11/11 exit=139（初轮×2、ASLR 开×3、关×3、单 CPU×3），GDB 2/2 正常（含 `CreateSyncPrim → 0`）。
   全轮精确计数：exit=139 共 13 次（rung4×2、rung5×11）；exit=0 共 9 次（rung1/2/3、rung4 standalone×3+GDB×1、rung5 GDB×2）。
5. core（`1cpu3` 轮，命令见 core 头）：`#0 RGXCreateRenderContextCCB ← #1 RGXCreateRenderContext ← #2 main:190`，`rax=0`，错指令 `mov 0x8(%rax),%rdi`；前文 `mov 0x8(%r12),%rax` 取对象子指针。另 `mov 0x54(%rax),%eax`（features+0x54 门控区）在错指令之前。完整笔录见 [`r167-render-segv.txt`](r167-render-segv.txt)，失败 trace（148 行，全 ret=0）见 [`r167-rung5-segv.jsonl`](r167-rung5-segv.jsonl)。
6. 会话健康：probe ref 1，bridge ref 0，`card1`/`renderD128` 在位；`dmesg` 桥错误计数 0，仅 arena-close 常规行。

## 边界

- “时序敏感”依据：同配方同桥在 GDB 下必过、standalone 高频崩；ASLR/单 CPU 对照已证伪这两个变量。发布者（哪个线程/哪次调用应填充 `r12+8`）未命名——候选下一步：shim trace 加 tid，比对通过/失败轮的调用交错。
- L4 rung6–8 被 rung5 阻塞；`translate_kick` 仍 off；无 GPU 工作提交；freeze 继续。

## 下一步（候选）

- trace 加 tid 后重跑 rung5 若干次，用交错差异定位 NULL 发布者（离线可先做 shim 改动，需重编 shim .so 但不动内核）。
