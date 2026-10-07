# r199：定位 RGXKickGfx 的 SubmissionCmdGenerate allocator 参数链（静态指令核对，动态待证）

## 结论

r198 在 `SubmissionCmdGenerate` 入口看到首参为 0；同 SHA UMD 指令显示，该首参不是 `SubmissionHead`，而是从 GFX kick 输入 `psKickTA+0x28` 指向对象的 `+0x200` 读取。render-context 构造器会把 `SubmissionBufAlloctor` 建在 render context `+0x200`，并把它传给 `SubmissionCmdGenerate` 的对象应能提供这个 allocator。下一轮应在同一 fabricated replay 中逐项 dump `psKickTA+0x28`、其目标 `+0x200`、`psKickTA+0x2d8` 及真实 render-context `+0x200`，再决定是否应把 render-context 句柄填入 `psKickTA+0x28`。本轮尚未动态确认输入字段身份或修正重放。

## 静态指令与语料证据

- 本轮先实测离线 UMD SHA-256：`b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与 `DECOMPILATION.md` 一致；以下只按该版二进制与语料核对。
- `RGXKickGfx` 的真实 ELF 指令（`objdump -d --disassemble=RGXKickGfx`，RVA `0x7ebc0–0x7ebe0`）先从 kick 输入基址 `r14+0x28` 取指针，再从目标 `+0x200` 取值到 `rdi`，随后调用 `SubmissionCmdGenerate`。因此 r198 观察到的空首参来自这条两级指针链的终值；仅凭当时的 `rdi=0` 不能区分输入目标错误还是 allocator 槽为空。
- `RGXCreateRenderContextCCB` 的二进制指令在 RVA `0x7c126–0x7c132` 将 render-context `r12+0x200` 作为 `SubmissionBufAlloctorCreate` 输出槽；成功后立即读取该槽并初始化 allocator `+0x48`。这提供了正常 context 构造出的 allocator 来源。
- 同一构造器另在 RVA `0x7c862–0x7c86a` 以 `render-context+0x318` 调用 `SubmissionHeadCreate`。而 `RGXKickGfx` 调用 `SubmissionCmdGenerate` 时把新建的 `local_eb8[0]` 作为第二参。因此 `SubmissionHead` 与首参 allocator 是不同对象；r198 所写“submission-context 首参来源”应具体化为 allocator 指针链。
- 同 SHA 语料 `decompiled.c:53703`、`54731–55121`、`36899–36958` 与上述指令一致：render context 保存 allocator 于 `+0x200`；`SubmissionBufAlloctorCreate` 输出一个 0x50 字节对象；kick 输入 `+0x28` 的目标 `+0x218` 还作为 CSW buffer/list 传入 `SubmissionAddCswBuf`。

## 边界与下一步

以上定位了指针来源及候选初始化者，没有证明 r198 的 kick 输入 `+0x28` 应当等于 render-context 句柄，也没有证明把它补齐后 `RGXKickGfx` 可以走到 update helper。继续使用 AppHint 初始化，不改写 render-context 内存；在 GDB 断点处逐级读取：kick `+0x28`、其指向对象 `+0x200`、kick `+0x2d8`、由 `RGXCreateRenderContext` 返回的对象 `+0x200`。对齐链条后再用合法输入重放并观察 `SubmissionSetUpdateSyncPrim` 与 fabricated bridge trace。全程保持活会话 freeze。

零硬件触碰；本轮无代码改动、无动态重放、未运行测试套件。
