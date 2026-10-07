# r210：fabricated GFX producer 首次落出 UMD 原始 CCB（零硬件触碰）

## 结论

恢复 r203 GFX producer 的关键输入后，fabricated `RGXKickGfx` 干净返回 0；shim 在 `0x82:0x14` 调用处捕获到 UMD 提交 backing 中的 0x4700 原始 CCB 字节。该结果闭合了 r209 捕获器的离线端到端路径，但 shim 仍回零值，不证明 bridge handler、同步语义或 GPU 执行正确。

## 实测

- 全程零硬件触碰：`umd_connect_harness` + `umd_bridge_shim.so`；未设置 `UMD_SHIM_PASSTHROUGH`，无模块、真实 DRM、PCI 或 GPU 操作。UMD SHA-256 为 `b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0`，与 `DECOMPILATION.md` 匹配。
- 重放进程以 `mt-vgpu-guest/build/r210-replay` 为工作目录，使 UMD 读取该目录的 `musa.ini`（`PerfCountStartCbID=0`、`PerfCountEndCbID=0`）。从仓库根目录启动会在 PrepareTA 中用未初始化的 PerfCountEndCbID 形成越界索引；使用配置目录后 PrepareTA 返回 0。
- fabricated fixture 创建 device/mem/render context 和 sync primitive；GFX kick 给 `b24`、`b25` 各分配 0x410 字节，设置 allocator、render context、CCB 输出字段及 sync 输入。工作缓冲中将 `b22+0x48` 设为 sync handle、`b22+0x50` 设为 `0x1234`；`b20+0x6f0` 设为 1，sync tuple 写在 `b20+0x2f0` 至 `+0x308`。GDB 实测 check tuple `{count=1, handle!=0, value=0x1234}`，update helper 输入含同一非空 handle、value=`0x1235`，两个 helper 均返回 0；`RGXKickGfx(...) -> 0`，进程正常退出。
- trace seq 125 为 `0x82:0x14`（IN 108 / OUT 4，shim fabricated 零返回），解码出 submission VA `0x8000023000`、size `0x4700`、submission ID 1、check/update count 均为 1。
- 原始字节保存在 [`r210-gfx-ccb-capture.bin`](r210-gfx-ccb-capture.bin)，长度 18,176 字节，SHA-256 `faa93985aa6b3f65df66641ac73f25020a66fca7af6cdece3b9e88c3a315dae7`，文件权限 0600。共 107 个非零字节。完整桥 trace 在 [`r210-gfx-ccb-capture.jsonl`](r210-gfx-ccb-capture.jsonl)。

## 推断与边界

这证明同 SHA 5.2 UMD 在受控 fabricated 输入下生成了可从 submission VA 读取的 CCB backing 字节。稀疏字节分布本身不能证明该包可被目标 Guest 接受、资源引用有效或 GPU 能执行。此次没有改代码；没有运行门禁；活会话继续 freeze。

## 下一步

以该原始 CCB 为离线输入，按 UMD 生成路径辨识包头与 payload 边界，核对嵌套资源/同步引用，再继续实现真实 DDK2 render backend 与 TA/3D 执行链。任何真实 CCB live 验证仍需用户明确批准。
