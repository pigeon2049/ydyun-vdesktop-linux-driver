# r155：fabricated PMR 映射可共享并区分句柄，SubmitTransfer2 仍无真实 GPU 结果

- **结论**：新增 opt-in shared-backing 模式后，同一 Services mmap handle 的重复映射共享字节，不同 PMR 使用不同 backing offset；仅 AcquireInfoPage 对应 PMR 写入合成 info-page 头。离屏 blit 再次到达 SubmitTransfer2，但 fake ioctl 不执行 GPU，像素校验仍失败，尚未确认绘制 CCB。本轮零硬件触碰。

## 实测

1. `UMD_SHARED_BACKING=1` 为每个 Services mmap handle 创建独立 memfd；同 handle 重复映射共享字节，不同 PMR 即使长度大于相邻 handle 的 offset 间隔也彼此隔离。fabricated `0x6:0x6` LocalImportPMR 在此模式下返回递增 mmap handle，避免原 canned `0x1001` 让 info-page 与后续 PMR 错误别名。旧 fabricated 行为保持默认，新的映射模式显式 opt-in。
2. 新离线集成测试实际编译 shim 与小型 syscall/ioctl 客户端：同 offset 写入可从 alias 读回，不同 handle offset 仍为零，info-page 头只出现在 AcquireInfoPage 导入项。定向测试通过。反向注入 `MAP_SHARED`→`MAP_PRIVATE` 后测试以客户端码 13 失败，恢复后通过。
3. 5.2 UMD SHA-256 仍为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`。`musa_blit_test -device 0 -f -o` 在 shared-backing 模式完成 TransferContext 创建、Submit、Wait 并清理，像素比对失败。trace 共 594 行、169 条 bridge ioctl，在 seq 524 到达 `0x89:0x4`（108B IN/8B OUT），未见 `0x89:0xa`；没有 ioctl passthrough。完整轨迹：[`r155-shared-fabricated-pmr.jsonl`](r155-shared-fabricated-pmr.jsonl)。
4. Submit 前的 16 个 PMR snapshot 显示 info-page 映射有 7 个非零字节；两个由 `0x89:0x5` 返回并经 LocalImportPMR 映射的 TDM 区域 `0x1003000`、`0x1004000` 各 64KiB，采样时非零字节数均为 0。TQCB PDS/DMA/TEX mappings（`0x5006000`–`0x5008000`）也全零；`0x5009000`、`0x500a000`、`0x500b000` 分别有 30、132、95 个非零字节，较大的 `0x500c000` 有 2,621,440 个非零字节。snapshot 只给出首个非零窗口与计数，尚未将任何区间认定为 CCB。
5. `make -C mt-vgpu-guest check-offline` 通过：265 Python（1 skip）与 272 C checks；shim 通过 `-Werror` 编译。未运行硬件或内核门禁。

## 推断与边界

- shared-backing 让 fabricated UMD 的 PMR 写入与 aliases 可观测，消除了“每次 mmap 都是零匿名页”的限制；Submit 的返回值与 Wait 仍来自 shim，不能代表 bridge 或 GPU 执行成功。
- TDM shared-memory 区域在 submit 时仍为零，因而本次轨迹没有直接证实其中含有绘制 CCB。后续应对照 5.2 UMD 的 `SubmissionCmdGenerate()` 数据流，定位非零 TQCB/池 PMR 中的 command bytes，再核对 `0x89:0x4` 指针、长度和同步字段；保持离线，不接入未经验证的 Translator packet parser。
