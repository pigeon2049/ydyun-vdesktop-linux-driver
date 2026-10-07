# r209：为 fabricated `0x82:0x14` 重放补上可选原始 CCB 捕获

## 结论

零硬件触碰。扩展离线 `umd_bridge_shim`，使共享 PMR backing 模式能按 5.2 `MUSAKICKGFX5` 的 `submission_va@76`、`submission_size@84` 定位并解析 `0x82:0x14` CCB；只有同时启用 `UMD_SHARED_BACKING=1` 和显式 `UMD_CCB_DUMP_DIR` 才会将可解析的原始字节单独保存，最大 16 MiB、独占创建、权限 `0600`。现有 `0x89:0xa` CCB 解析复用同一路径。

新增合成门禁证实字段偏移、PMR backing 解析、原始字节逐字节落盘、不可解析 VA 拒绝落盘、缺少 shared backing 或 dump 目录时不写文件。把 `submission_size` 偏移故意改成 80 后，门禁因解析成 128 而非 256 失败；恢复 84 后全绿。

这给下一轮重新运行 fabricated GFX producer 并检查实际 UMD CCB 字节提供了采集能力；本轮没有复跑 GFX producer，也没有捕获真实 UMD 生成包。既有 r203 fabricated trace 的请求字段按新 schema 解码为 VA `0x8000023000`、长度 `0x4700`、flags=0、submission ID=1；trace 证明的是 UMD 发包，原始 backing 字节仍未取得。fake shim 返回零，不表示 handler 接受或 GPU 执行。

## 验证

- `python3 -m unittest tests.test_pvr_shim_ccb_resolve`：通过。
- 反向验证：注入错误 size 偏移 `80` 后测试失败（捕获到 128 vs 256），还原 `84` 后通过。
- `make -C mt-vgpu-guest check-offline`：295 Python（1 skip）和 292 C RAM checks 全绿。
- `git diff --check`：通过。未改内核代码，因此未运行 `make kernel`；未做硬件操作。

## 边界

捕获仅覆盖 shim 能通过 fabricated VA reservation、mapping 与 mmap backing 台账解析的请求；它不验证 DM2 packet 格式、资源引用闭包、cache/fence 语义或硬件执行。捕获文件是调试证据，不是门禁产物。
