# r153：单核 fabricated 回包解除 Context 阻塞，Rogue2D 创建上下文成功

- **结论**：用现有 harness 重放单次 `R2DCreateContext`，UMD 返回 0；两个 `0x1:0xc` 回包均为 `num_cores=1`，轨迹进入 fabricated `0x89:0x5` 与 `0x89:0x0`。共 95 条 bridge 调用，未到 `0x89:0xa SubmitTransfer3`。本轮零硬件触碰。

## 实测

1. 运行前核对 `libsrv_um_MUSA.so.1.0.0` SHA-256 为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与 `DECOMPILATION.md` 一致。harness 经 `LD_PRELOAD=umd_bridge_shim.so` 运行，DRI 节点打开被 shim 映射到 `/dev/null`；ioctl 与 mmap 均走 fabricated 路径。
2. 调用 `R2DCreateContext(out)` 返回 0。trace 共 123 行，其中 95 条 bridge ioctl。两次 `0x1:0xc` 都是 12B/16B，`out_written` 尾部为 `01000000`（`num_cores=1`）。随后 `0x89:0x5` OUT 两个 fabricated handle 非零，`0x89:0x0` 返回 fabricated context handle，均 `ret=0`。
3. 当前 `/dev/dri` 只见 `card0`。`sutu_dev_select(128)` 打印 device out-of-range（函数仍返回 0），`sutu_dev_select(0)` 返回 1；两者之后追加 `R2DCreateSurface`（宽高 `0x40`、格式值 `4`、另传 64B 参数区）都返回 3。再直接调 `R2DCreateSurfaceLayout` 并提供 11 个参数也返回 3。三种扩展调用的 trace 都停在相同 95 条 bridge ioctl，没有 Surface 阶段的新 ioctl。测试参数来自已有离线调查，但完整 Surface ABI 尚未证实，故只记录边界，不把返回 3 归因到唯一内部条件。
4. 另外在同一 shim 下无参数运行候选 `musa_blit_test`，进程以 SIGABRT 退出；gdb 栈落在 stripped `libsutu_display_MUSA.so` 的 abort 路径，未保留内部符号，无法归因。其 49 条 bridge ioctl 未进入 `0x89` 组；不将这次无参数运行视为适配结论。trace：[`r153-musa-blit-noargs.jsonl`](r153-musa-blit-noargs.jsonl)。
5. context-only 原始 bridge trace：[`r153-rogue2d-context.jsonl`](r153-rogue2d-context.jsonl)。另一次等价重放打印了 256B `R2DCreateContext` 输出区，主要是进程内指针，本报告不保存该 dump。harness 重放进程均正常退出。

## 推断与边界

- r150 定位的零核/零尺寸 TDM store 阻塞已在 fabricated 环境中越过；这验证的是 UMD 对单核输出的行为和 bridge 调用顺序，不是内核实现或硬件 TDM 行为。
- `R2DCreateContext` 单次调用只创建并清理 context，不产生绘制 CCB。要继续到 Surface/SubmitTransfer，需要 Rogue2D 的 surface、layout 与绘制前置调用及其输入结构；不能把 95 条成功 fabricated bridge 调用外推为可提交工作。
- 下一步保持离线：继续查明 Surface 调用所需的有效 descriptor/device 前置条件，使用可复现输入观察是否出现 Surface 阶段桥调用，再观察非零 CCB 与 `0x89:0xa`。硬件 bridge 仍因 Oops 栈缺失不重载。
