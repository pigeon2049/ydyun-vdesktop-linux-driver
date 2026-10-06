# r157：离线 blit 在 drm_major=2 到达 SubmitTransfer3，CCB backing 尚待定位

- **结论**：`UMD_DRM_MAJOR=2` fabricated replay 使已知离屏 blit 进入 `0x89:0xa SubmitTransfer3`，输入含 CCB GPU VA `0x8000f44000`、长度 `0x1200`。Submit 前 shared-backing snapshot 记录 15 个当前映射；其中一个 General pool backing 有 2,621,440 个非零字节，但尚不能把它认定为该 CCB。本轮零硬件触碰。

## 实测

1. 核对 `libsrv_um_MUSA.so.1.0.0` SHA-256 为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与 5.2 语料一致。`musa_blit_test -device 0 -f -o` 在 shim 的 `UMD_DRM_MAJOR=2`、shared backing 与 snapshot 模式下运行；trace 共 527 行、105 条 bridge ioctl、15 条 `0x89:0xa` 前 PMR snapshot，没有 ioctl passthrough。完整证据见 [`r157-ddk2-tq-submit.jsonl`](r157-ddk2-tq-submit.jsonl)。
2. SubmitTransfer3 输入为 108B，OUT 为 4B。按 r151 的 packed ABI 描述解码，trace 中 `check_count=0`、`update_count=2`、`pmr_sync_count=0`、`ccb_data=0x8000f44000`、`ccb_bytes=0x1200`；这些值来自运行时 ioctl 字节，字段解释仍以核对过的 5.2 wrapper 为前提。shim 返回 fabricated 零值后 UMD 仍未退出；检测到该 ioctl 后即终止进程，避免把伪造完成当成执行成功。
3. Submit 前 `0x1003000/0x1004000` TDM shared PMR 与 `0x5006000`–`0x5008000` TQCB PDS/DMA/TEX 区域均为零。`0x500d000` 映射长 5,246,975 字节，含 2,621,440 个非零字节；`0x500e000` 映射长 10,489,855 字节，含 39 个非零字节。`0x500a000`/`0x500b000`/`0x500c000` 分别有 30/132/95 个非零字节。snapshot 尚未将 GPU VA 关联到任一 PMR，因此不据此认定 CCB 所在区。
4. 新增 shim opt-in `UMD_DRM_MAJOR=2`（默认仍为 1）及 `0x89:0xa` snapshot 触发。发现 snapshot registry 会保留已解除映射地址，新增 munmap 清理与 snapshot/unmap 锁序，避免读取 stale mapping。反向注入固定 major=1 时 DDK2 门禁测试失败；反向注入保留 stale PMR entry 时离线客户端以 SIGSEGV（-11）失败；恢复后定向测试通过。
5. `musa_tq_performance_test -n 1` 在 major 2 fabricated 模式于提交前 SIGABRT（-6），未到 `0x89:0xa`。`make -C mt-vgpu-guest check-offline` 通过：268 Python（1 skip）+272 C；shim `-Werror` 编译通过。

## 下一步

用 `ccb_data` GPU VA 与 UMD heap/suballocation 元数据建立 GPU VA → PMR mapping → CPU backing 的对应关系，再只读转储 `0x1200` 字节验证是否与 Submit3 所指内容一致。当前 trace 只证明 UMD 生成了 Submit3 输入，不证明 bridge 接受、命令有效或 GPU 执行成功；不重载硬件 bridge。
