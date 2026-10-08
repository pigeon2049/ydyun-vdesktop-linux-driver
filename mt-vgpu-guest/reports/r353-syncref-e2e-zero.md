# r353：T2-f——回填真描述子后 `SyncPrimRef` 端到端返回 0（离线 fabricated，零硬件触碰）

- **结论（实测）**：在 `0x79c92`（`mov 0x48(%rdx),%rdi`）处把 b10 真描述子 poke 进槽 0（`slot=$rdx+0x48`，原值 NULL）后，完整 `RGXKickTA` 路径上 `SyncPrimRef` **两次返回 0**（成功），`SubmitTA` 未跳 `0x7ada7` 错误出口、正常继续。T2 系列核心问题（`SyncPrimRef` 报 3 的 `INVALID_PARAMS`）就此闭合。
- **结论（实测）**：`SyncPrimRef` 成功之后、下游在偏移 `0x929ce` 处触发 SIGSEGV（新现象；此前被 `SyncPrimRef→3` 挡住从未到达）。该崩溃点超出 T2 范围，记为 T2-g/新系列起点。
- **方法（实测）**：GDB 从头运行 + `set disable-randomization off`（保持 ASLR，与原生一致）可稳定复现完整路径，绕开 r352 的堆布局敏感崩溃；GDB Python 在 `CreateSyncPrim` 返回地址设临时断点捕获 b10 描述子（`desc+8=1` 验证通过），在 `0x79c92` 按 `$rdx+0x48` 定位槽位 poke。两次独立运行结果一致（确定性）。
- **关键细节（实测）**：`0x79c92` 处 `$rbx` 已被 `0x79ba0` 改写，不能用 r351 的 `rbx+208*i` 公式直接算——必须用已算好的 `$rdx`（`=rbx+208*i`）加 `0x48`。此前按 `$rbx` 取槽位会读到垃圾地址（`0x49` 非法访问）。
- **教训（实测）**：
  1. GDB 默认关闭 ASLR 会触发该 UMD 的堆布局敏感崩溃（`RGXCreateRenderContextCCB+1525`）；`set disable-randomization off` 后原生与 GDB 行为一致。此前 r352 归因于"GDB 引入"不够精确，应为"GDB 默认关 ASLR 引入"。
  2. harness 参数若不经 `eval`，`'b5*+0'` 的字面引号会传给 harness，`parse_arg` 回退到 `strtoull` 得 0，导致 `b6+16` 被置 0、`RGXCreateRenderContext` 段错误。本轮改用 `set -f` + 去引号解决。此前误判为 `nohup` 触发崩溃，实为引号 bug，特此纠正。
  3. GDB Python 的 `stop()` 内不可 `gdb.execute("finish")`（报 "thread is running"）；改用返回地址临时断点捕获 OUT 参数。

## 实测与边界

1. 全程 fabricated（harness + GDB Python，无模块、无 DRM、无 PCI、无 GPU）；UMD SHA `b3058c02…34237b0` 对版。会话 freeze 继续。
2. 证据（0600）：`r353-t2f-poke-observe.txt`（两次运行的 T2F-CAP/POKE/RET/PATH 完整标记）。
   - `T2F-POKE slot=0x… rdx=0x… old=0x0 desc=0x… verify=0x…`（old=0 证实槽位原为 NULL）
   - `T2F-RET SyncPrimRef -> 0 [success-0]` ×2，`T2F-PATH SubmitTA continued past SyncPrimRef`
3. 未断言：`0x929ce` SIGSEGV 的根因（反汇编显示该处字节疑似非指令边界，需 T2-g 用 GDB 断点精确定位）；`i=*(rbx+0x24)` 在真实 kick 下是否恒为 0。
4. 本轮未改驱动代码（仅 harness 调用方式与 GDB 脚本），`check-offline` 门禁沿用。

## 下一步

1. T2-g：定位 `0x929ce` SIGSEGV——在该偏移附近设 GDB 断点，确认崩溃指令与寄存器状态，判断是 fabricated 路径 artifact 还是真实下阶段问题；仍离线、零硬件触碰。
