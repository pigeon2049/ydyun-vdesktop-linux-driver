# r156：SubmissionCmdGenerate 属于 DDK2 路径，离屏 blit 仍走 legacy SubmitTransfer2

- **结论**：5.2 UMD 语料和 r155 运行轨迹确认两条提交链不同。`musa_blit_test` 走 legacy `0x89:0x4`，没有调用 `SubmissionCmdGenerate()`；该生成函数属于 `TQJobSubmit` / `TQJobMultiSubmit` 的 DDK2 路径，并通过 `FUN_0015f890` 走到 `0x89:0xa`。不能用 legacy 包或 PMR snapshot 推断 DDK2 CCB 格式。本轮零硬件触碰。

## 实测

1. 再次实测 `libsrv_um_MUSA.so.1.0.0` SHA-256：`b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，匹配 `DECOMPILATION.md` 所指 5.2 语料。
2. GDB 在 fabricated `0x89:0x4` ioctl 前停住：IN 长 108B，包含 transfer-context handle、计数/标志和多个指向当前栈的嵌套指针。抽查这些嵌套块是 sync/update 元数据或局部缓冲，未识别到 CCB 字节流；不能据此否定其他 PMR 中存在 command data。
3. 同一 shared-backing replay 的 16 个 PMR snapshot 显示：TDM shared 区 `0x1003000/0x1004000` 和 TQCB PDS/DMA/TEX 区 `0x5006000`–`0x5008000` 在 Submit 前非零字节均为 0。`0x5009000`、`0x500a000`、`0x500b000` 分别含 30、132、95 个非零字节，`0x500c000` 含 2,621,440 个非零字节；这些内容用途尚未从 runtime 证实。r155 原始证据见 [`r155-shared-fabricated-pmr.jsonl`](r155-shared-fabricated-pmr.jsonl)。

## 语料静态结论（反编译假设，非本轮 runtime 实测）

- `functions.jsonl` / `calls.jsonl` 中 `TQJobSubmit`（`0015ff00`）和 `TQJobMultiSubmit`（`00160970`）调用 `SubmissionCmdGenerate`（`0015f020`）。伪 C 显示它组装头部、命令区和变长区域，并返回生成 buffer 的 GPU VA 与长度；这说明它是 DDK2 command-generation 路径。
- 两个 producer 随后调用 `FUN_0015f890`（`0015f890`）；calls 语料显示该函数调用 `FUN_001383d0`，r151 已核对该 wrapper 对应 `0x89:0xa SubmitTransfer3`。
- 当前 legacy blit 的函数链是 `RGXTDMQueueTransfer` → `RGXTDMSubmit` → `FUN_001646f0` → `FUN_00138280`。`FUN_00138280` 的 5.2 伪 C 构造 `0x89:0x4 SubmitTransfer2` 的 108B 输入；calls 语料也确认 `001646f0` 调用 `00138280`。本次 trace 与该链一致，没有 `0x89:0xa`。

## 下一步

改为离线寻找可复现的 DDK2 producer 输入，使运行轨迹实际进入 `SubmissionCmdGenerate()` / `FUN_0015f890` / `0x89:0xa`；再用 shared-backing 和 PMR snapshot 验证其生成的 CCB。legacy blit 路径的数据保留作对照，不把它映射到 SubmitTransfer3 ABI，也不执行真实 GPU 工作。
