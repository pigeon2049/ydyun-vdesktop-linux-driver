# r198：musa.ini 合法设置 PerfCountEndCbID 后越过 PrepareTA；SubmissionCmdGenerate 仍缺 context（fabricated，零硬件触碰）

## 结论

用同 SHA 的 UMD 与 fabricated bridge 重放时，不手动 poke render-context 内存，直接创建 `musa.ini` AppHint 将 `PerfCountEndCbID=0`，context 的 `+0x24` 即为 0，`RGXKickGfx` 越过先前的 `RGXPrepareTA` 崩溃点。随后仍在 `SubmissionCmdGenerate` 因首参为空 SIGSEGV；bridge trace 没有 `0x82:0x14`。这确认 `+0x24` 不只是 AppHint 命名字段：PrepareTA 还把它作为 context 状态表索引。它不是单独 update-list 计数。

## 实测

- UMD SHA-256：`b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，匹配 `DECOMPILATION.md`。运行于 LD_PRELOAD fabricated bridge、`UMD_DRM_MAJOR=2`，无模块加载、PCI、DRM/GPU 工作。
- 在没有 AppHint 文件、没有 poke 的基准重放中，`RGXCreateRenderContext` 输出对象 `+0x20` 为 0、`+0x24` 为 `0xffffffff`。GDB 在 `FUN_00178800` 停于 ELF RVA `0x7893a`（Ghidra address `0x17893a`），故障指令为 `mov (%rsi),%rax`；该地址由 context 状态表索引计算得到，索引为 `0xffffffff`。trace 没有 `0x82:0x14`。
- 第二次重放在独立临时工作目录放置：

  ```ini
  [default]
  PerfCountStartCbID=0
  PerfCountEndCbID=0
  ```

  UMD 的 `PVRSRVCreateAppHintState` 会从当前目录 `musa.ini` 读取 AppHint；GDB dump 显示 context `+0x20/+0x24` 均为 0，整个 harness 命令未执行对象内存 poke。`RGXKickGfx` 越过 PrepareTA，并在 `SubmissionCmdGenerate+33`（ELF RVA `0x5f041`）因首参 `rdi=0` 崩溃。109 条 fabricated trace 无 `0x82:0x14`。
- 此次 psKickTA 输入手工设置了 `param_2[0x1bc]=1`，一项首字段 `2`，并传入本轮 `CreateSyncPrim` 返回的 UMD sync handle；但还未到 `SubmissionSetUpdateSyncPrim`，因此不能声称列表 count/flag 已经在 helper 端动态确认。

## 语料对照与解释边界

- `RGXCreateRenderContextCCB` 将 AppHint `PerfCountStartCbID` / `PerfCountEndCbID` 写入 render context `+0x20/+0x24`；End 的无配置默认值是 `0xffffffff`（同 SHA corpus `decompiled.c:53544–53548`）。
- `RGXPrepareTA` (`FUN_00178800`) 从该 context `+0x24` 取值参与状态表地址计算（`52089–52106`），这是本轮 GDB/指令实测确认的用途之一。另一方面，它另行分配 update-list，并将**新列表对象** `+0x24` 计数设 0，再复制调用者数组（`52242–52264`）。两处 `+0x24` 属于不同对象。
- `musa.ini` 可以经正常 AppHint 初始化 context 字段，不需要 harness 对象 poke；当前证据只确认 `0` 可越过 PrepareTA 故障，不证明该设置在真实运行时的语义合适，也未形成真实绘制或 update bridge 请求。

## 下一步

查明 `RGXKickGfx` 传给 `SubmissionCmdGenerate` 的 submission context 来源为何为 null。优先沿 `RGXCreateRenderContextCCB` 创建的 SubmissionHead/region 与 psKickTA 输入之间对照，再用 fabricated GDB 逐项补齐真实 producer 输入；保持 AppHint 配置路径，不直接改写 context 内存。完成 helper 观测前不宣称 update producer 已动态复验；活会话保持 freeze。

完整 trace：`r198-gfx-apphint-gdb.jsonl`。


## 补充更正（r199）

r198 将 `SubmissionCmdGenerate` 的空首参笼统记为缺少 submission context。r199 按同 SHA UMD 指令定位：首参来自 `psKickTA+0x28` 所指对象的 `+0x200`，其预期对象与 allocator 的动态身份尚待 GDB 逐级确认；`SubmissionHead` 是单独传入的第二参。见 [`r199-gfx-submission-allocator-origin.md`](r199-gfx-submission-allocator-origin.md)。
