# r203：扩大 GFX 输出缓冲后修复 fabricated heap 覆写，RGXKickGfx 干净返回

## 结论

r201 的 heap abort 是 harness 输入缓冲过小造成的越界复制。`RGXKickGfx` 成功路径执行 `rep movsq`，从 `param_5+8` 向 `param_3+8` 复制 0x408 字节；旧重放给两个缓冲分别只分配 0x300 和 0x80 字节。GDB watchpoint 实测该复制指令把 RGXPrepareTA 刚分配的 update-list chunk size 从 `0x91` 改成 `0x1151`，最终 free 因损坏 abort。将两个缓冲都扩到 0x410 后，update-list header 保持 `0x91`，`0x82:0x14` 仍被发出，`RGXKickGfx` 返回 0，进程正常退出。

## 实测

- 零硬件触碰：默认 fabricated shim；未设置 `UMD_SHIM_PASSTHROUGH`，无模块、真实 DRM、PCI 或 GPU 操作。UMD SHA-256 为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`。
- 在 `RGXPrepareTA` 的 update-list 分配后，GDB 见指针 `0x555555583dc0`，malloc header size=`0x91`（0x80 请求加 allocator header），首项为 flag=2、非空 sync handle。
- 旧尺寸重放中，`b24=0x555555583a20`（0x80 bytes），`b25=0x555555583ab0`（0x300 bytes）。UMD 二进制 RVA `0x7ee1a` 的 `rep movsq` 从 `b25+8` 向 `b24+8` 复制 0x81 个 qword（0x408 bytes）。watchpoint 在该指令处触发，update-list header `0x91 → 0x1151`；最终 free 的参数仍为 `0x555555583dc0`，随即 glibc 报 `double free or corruption (!prev)`。因此是复制写越界破坏相邻 update-list chunk metadata，不是 update-list 自身重复释放。
- 修正为 `b24=0x410`、`b25=0x410` 后，final free 前该 update-list chunk size 仍为 `0x91`；`SubmissionSetCheckSyncPrim` 与 `SubmissionSetUpdateSyncPrim` 均 count=1，update 项 flag=2；trace seq 123 发出 `0x82:0x14`（IN108 / OUT4），shim fabricated 零返回；`RGXKickGfx(...) -> 0`，进程正常退出。trace 见 [`r203-gfx-update-clean.jsonl`](r203-gfx-update-clean.jsonl)。

## 边界与下一步

本轮修正的是离线 harness 缓冲区尺寸，未改驱动或 UMD；fake shim 不执行真实 `0x82:0x14` handler，故不证明同步语义或 GPU CCB 正确。GFX producer 现在可在 fabricated 模式干净返回；下一步离线核对桥协议缺失 handler 的 ABI/结构布局，之后再按硬件批准流程验证真实 CCB。活会话继续 freeze。
