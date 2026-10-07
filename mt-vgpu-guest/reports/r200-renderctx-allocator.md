# r200：fabricated 创建证实 render context 正常持有 allocator 与独立 SubmissionHead

## 结论

同 SHA UMD 在默认 fabricated shim 下执行 `connect → device → devmemctx → render context` 均返回 0。运行时读取 render-context 返回对象，`+0x200` 是非空 `SubmissionBufAlloctor` 指针，`+0x318` 是另一非空 `SubmissionHead` 指针，与 r199 反汇编得到的两条构造路径一致。这证明本次正常创建的 render context 已初始化 allocator；尚未调用 `RGXKickGfx`，不能据此确定 r198 kick 输入 `+0x28` 的目标，也未到达 `SubmissionCmdGenerate`。

## 实测

- UMD SHA-256：`b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与 `DECOMPILATION.md` 一致。
- 运行 `build/probe/umd_connect_harness`，未设置 `UMD_SHIM_PASSTHROUGH`，使用 shim 默认 fabricated 模式。`PVRSRVConnectionCreateDevice`、`RGXCreateDeviceMemContext`、`RGXCreateRenderContext` 均返回 0；render context 输入按仓库配方设置 `+0x10` 为 devmem context、`+0x30/+0x34` deadline 为 1。无模块、真实 DRM ioctl、PCI 或 GPU 工作。
- harness 在返回的 render-context 指针 `b9*` 上 dump：`b9*+0x200` 起始 qword 非零（运行时地址 `0x55d7b0f6d740`），是 allocator 对象指针；`b9*+0x318` 起始 qword也非零（`0x55d7b0f6e070`），是 SubmissionHead 指针。地址是本次进程内瞬时值。
- fabricated trace 共 117 行，桥请求由 shim 回答；完整记录见 `r200-renderctx-allocator.jsonl`。

## 解释边界与下一步

实测确认了 render-context 构造器会产生非空 allocator 与独立 SubmissionHead，排除了“正常 render context 构造普遍不创建 allocator”的可能；它不证明 r198 的 `psKickTA+0x28` 指向这个 render context。下一轮恢复 GFX kick fabricated 配方，在 call-site 断点逐项记录 `psKickTA+0x28`、目标 `+0x200`、`psKickTA+0x2d8`、render-context 返回对象及各自 allocator 指针；再根据观测修正调用方输入并复放 update helper。活会话继续 freeze。

零硬件触碰；本轮无代码改动。未运行测试套件。
