# r197：纠正 r196 的 slot 解释；update 列表由 RGXPrepareTA 构造（静态语料，零硬件触碰）

## 结论

r196 手动写零的对象偏移 `+0x20..+0x27`，按同 SHA 的 `RGXCreateRenderContextCCB`
语料是 `PerfCountStartCbID` / `PerfCountEndCbID` 两个 AppHint 字段，并非 GFX
update 列表计数。r196 把它称作 “render-context slot” 是错误解释，应以本报告更正。
同版 `RGXPrepareTA`（`FUN_00178800`）在 feature `+0x54 >= 2` 路径中分配单独的
update-list 对象、将其 `+0x24` 计数置零，再从调用者的 `psKickTA` 输入数组复制
update 项。因此正常的 update-list 初始化位于 producer 内部；要重放的输入是
调用者结构中的条目，而不是改写 render-context perf 字段。

## 语料核对

- 执行前对当前离线 UMD 实测 SHA-256：
  `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与
  `DECOMPILATION.md` 一致。以下行号均来自这个版本的只读 Ghidra corpus，伪 C
  按仓库 §9 仅作路径假设。
- `RGXCreateRenderContextCCB`（corpus `decompiled.c:53386–53936`）在
  `+0x20` 调用 `PVRSRVGetAppHint("PerfCountStartCbID", ...)`；在 `+0x24`
  调用 `PVRSRVGetAppHint("PerfCountEndCbID", ...)`，调用前将 End 默认值设为
  `0xffffffff`（`53544–53548`）。r196 报告记录的手动 qword poke 覆盖的正是这两个字段。
- `FUN_00178800`，RGXKickGfx 调用的 prepare helper（`52052–52267`），在
  feature version `>= 2` 时执行：`param_2[0x1bc]` 是输入条目数；分配
  `((count+3)*0x20)` 字节并将新列表 `+0x24` 计数初始化为 0（`52242–52248`）。
  每条目复制四个 64 位输入字段，逐条递增新列表计数（`52249–52264`）。
- `RGXKickGfx` 随后从该列表 `+0x24` 取条目数、从 `+0x40` 遍历条目，并把
  结果交给 `SubmissionSetUpdateSyncPrim`（`54841–54860`、`55149–55152`）。

## 边界与下一步

上述是匹配 SHA 的静态路径，不是本轮新的动态验证；不能据此断言应用构造字段
已完全理解。r196 的 fabricated trace 仍证明其那次运行发出了 `0x82:0x14`，
但此前“必须清 render slot 才能生成 update”的因果结论撤回。下一轮应复放
RGXKickGfx 时保留构造器生成的 perf callback 默认值，并在调用者 `psKickTA`
内按 `param_2[0x1bc]` 与相应条目数组构造输入；用 GDB 观察 prepare 产出的列表
计数/flag 是否仍为 1 / 2，再核对 bridge trace。仍只用 fabricated bridge，活会话
保持 freeze。
