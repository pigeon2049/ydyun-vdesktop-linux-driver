# r154：离屏 blit 进入 SubmitTransfer2，伪造映射未提供可验证 CCB

- **结论**：使用正确 CLI 在 fabricated shim 下运行离屏 fill，UMD 完成 Rogue2D/TransferContext 初始化并调用 legacy `0x89:0x4 SubmitTransfer2`（108B 输入、8B 输出）。Submit 与 wait 均由 shim 伪造成功，但像素校验失败；现有零页/假回包不能作为真实绘制或 CCB 内容证据。本轮零硬件触碰。

## 实测

1. 核对 `libsrv_um_MUSA.so.1.0.0` SHA-256 为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与 `DECOMPILATION.md` 一致。运行 `musa_blit_test -device 0 -f -o`，shim 将 DRM 节点、bridge ioctl 与 mmap 全部留在 fabricated 路径；trace 有 578 行、169 次 bridge ioctl，没有 `ioctl_passthrough`。
2. 程序输出 `Create transfer context for RGX OK`、`Submit transfer command OK`、`Wait for blit to complete OK`，最后因渲染目标不匹配退出，清理完成。trace 在 seq 508 到达 `0x89:0x4`，`in_size=108`、`out_size=8`，随后继续 PMR 清理；未见 `0x89:0xa SubmitTransfer3`。完整轨迹见 [`r154-musa-blit-offscreen.jsonl`](r154-musa-blit-offscreen.jsonl)。
3. 5.2 UMD wrapper 语料与入口 SHA 已核对。108B 包中 `+0x08` 为 TDM context `0xc000`；其余关键值主要是进程内指针、同步/计数元数据。GDB 在 ioctl 入口观察到嵌套指针所指的栈块，没有发现可直接识别的 CCB 字节流；这只能说明 CCB 不在这些已检查的参数块中，不能判定共享区中不存在 CCB。
4. seq 461/465 等记录显示 `0x89:0x5` 返回的 fabricated handles 后续被映射为匿名内存。shim 的 `syscall(SYS_mmap)` 路径会把每个 UMD fd 映射替换为独立匿名区，并向映射开头写入 info-page 字段；GDB 对一处 `0x1001000` / 64KiB 映射检查到该合成头部后其余抽样为零。该 fake backing 无法表示驱动 PMR 的真实共享写入，因此不能靠本次像素失败或内存抽样判定 UMD 是否生成了 CCB。

## 推断与边界

- r153 的 Context-only 重放未到提交；r154 证明现有 blit 可复现入口能够推进到 legacy SubmitTransfer2。SubmitTransfer2 与 r151 新增的 SubmitTransfer3 ABI 是不同桥函数，不能把本次结果写成 SubmitTransfer3 或翻译器 CCB 验证。
- 后续离线工作应先让 shim 保留可关联的 PMR backing（相同 handle/offset 映射共享同一缓冲区），再采集 submit 前后的内容差异，验证哪一段属于 CCB；在此之前不接入翻译 handler。真实硬件提交仍不在本轮范围内。
