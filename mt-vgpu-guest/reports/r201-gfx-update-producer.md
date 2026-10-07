# r201：fabricated RGXKickGfx 动态核对 allocator 链并发出 update bridge 请求

## 结论

默认 fabricated shim 下，GDB 证实 `RGXKickGfx` 将 kick `+0x28` 指向对象的 `+0x200` allocator 传给 `SubmissionCmdGenerate`；该地址与正常构造的 render context `+0x200` allocator 相同。修正 sync 创建调用中 output-slot/name 参数的区分后，`SubmissionSetCheckSyncPrim` 接受一项真实非空 handle，`SubmissionSetUpdateSyncPrim` 观察到一项 `flag=2` 的非空 update 项，随后进程发出 `0x82:0x14`（IN 108 / OUT 4）。shim 将返回区置零并返回 0；其后 UMD 在释放 `local_e70` 临时对象时因 `double free or corruption (!prev)` abort。因此已动态复验 producer 到 bridge 请求的路径，未得到干净的 `RGXKickGfx` 返回，也未验证真实 bridge handler 或 GPU 执行。

## 实测

- 零硬件触碰：`umd_connect_harness` + `umd_bridge_shim.so` 默认 fabricated 模式，未设置 `UMD_SHIM_PASSTHROUGH`；无模块操作、真实 DRM ioctl、PCI 访问或 GPU 工作。运行前确认 `/tmp` 使用率 1%。
- UMD SHA-256 实测为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与仓库 `DECOMPILATION.md` 所列版本一致。
- 在 `SubmissionCmdGenerate` 入口，GDB 观测 allocator=`0x55555557bd00`、head=`0x555555583e50`、kick=`0x555555580ef0`；kick `+0x28` 和 `+0x2d8` 均指向 `0x55555559e770`，目标对象 `+0x200` 与 render context `+0x200` 均为同一非空 allocator `0x55555557bd00`。这动态闭合了 r199 的指针来源假设。
- `CreateSyncPrim` 的第二参数是输出对象槽，第三参数是名字字符串。先前把名字 buffer 当作输出对象导致 `SubmissionSetCheckSyncPrim` 收到 handle 0；按正确 output slot 重放后，check 项为 `{flags=1, handle=0x555555580e30, value=0x1234}`，update helper 的首项为 `flags=2`、非空 sync handle，count=1。
- trace 最后一条是 seq 123 的 `0x82:0x14`，IN 108 / OUT 4，shim 记录 `fabricated_zero_out` 且 `ret=0`。随后 GDB 回溯落在 `RGXKickGfx` 内对 `PVRSRVFreeUserModeMem(local_e70)` 的清理调用（UMD RVA `0x7ee24`）；glibc 报 `double free or corruption (!prev)` 并终止进程。完整桥 trace 在 [`r201-gfx-update-producer.jsonl`](r201-gfx-update-producer.jsonl)。
- 另一次按 `Makefile` L4 recipe 重建但 kick 字段不等价的尝试在 `RGXKickGfx` 提交前报 `malloc(): invalid size (unsorted)`，没有发出 `0x82:0x14`；该次作为无效 fabricated 输入排除，不用于上述正向结论。

## 边界与下一步

当前证据只证明 UMD 生成了一条非零 update 项并发出 DDK2 `RGXKickTA3D5` 请求；fabricated shim 没有执行 bridge handler，返回零值也不等于真实同步语义正确。allocator 指针链与 r199 推断一致，但清理阶段 abort 的具体所有权/损坏来源仍未命名，且本次调用没有干净返回。下一步保留完全相同的成功输入，使用低扰动 GDB 断点记录 `PVRSRVFreeUserModeMem` 参数及 `local_e70` 的分配/写入者，先解释清理 abort，再复放到干净返回。活会话继续 freeze。

无代码改动；本轮未运行测试套件。
