# r207：固定 KickTA3D5 108/4 wire 布局，确认真实提交仍缺 backend 链

## 结论

在 `mt_pvr_wire.h` 加入了 `0x82:0x14` 的精确 108/4 请求结构和字段偏移静态断言，`test_pvr_wire_sizes.py` 将其绑定到 5.2 UMD size row。此改动只描述 wire 格式，没有加入 dispatch 或成功回包。

对照当前 Guest bridge 后，缺口不止 handler：render context 是只带 kind 的 per-file token，没有底层 MUSA render backend；sync token 可以追到 PMR，但实际译码提交的 `pvr_translate_kick()` 只处理 `0x88:0x4` 并发固定 empty marker；`0x89:0xa` 当前是 CCB window observer/dry-run，不读取嵌套 sync arrays，也不提交 UMD CCB。把 `0x82:0x14` 接到其中任一接口都会错误宣称工作已执行。

## 实测与代码核对

- 零硬件触碰；没有加载模块、触碰 PCI/DRM 或发出 GPU 提交。
- 5.2 Host 参考结构来自 hash 对版的 `reference/kmd-5.2.0-server-generated/common_musagfx_bridge.h`；`PROVENANCE.md` 记录 DKMS 包 SHA-256 `e3f684b1f7582fa0b399a234b945ad4539c565a46399f8294db91368fa32c62b`。新增 wire 结构按 flags `0x48`、VA `0x4c`、size `0x54`、submission ID `0x58`、三个 count `0x60/0x64/0x68` 固定偏移。
- `pvr_object` 只有 handle/kind/arg0/arg1。`pvr_cmd_render2_create()` 读取创建包后只 mint `MT_PVR_KIND_CONTEXT` token；不创建或记录服务端 render context。SYNC object 的 `arg0` 才关联其 PMR。
- `pvr_translator_resolve()` 可将 PMR 句柄或带 PMR 的 SYNC object 解析为 CPU backing/range；`pvr_translate_kick()` 是 kicksync3 的专用路径，它等待 sync 条件后通过固定 `MT_TRANSLATE_CMD_VA` / `MT_GFX_LINUX_PACKET_BYTES` 提交 marker，再在完成后写 update 值。它没有消费 `0x82:0x14` 的 `submission_va/size/id`。
- `pvr_cmd_tdm_submit3_observe()` 只验证 TDM context token、CCB 落在已映射 reservation/PMR 中并记录窗口摘要；注释和实现均显示不读取 check/update/PMR 嵌套数组、不上传页表、不产生 GPU fence。
- 验证：`make check-offline` 全绿（295 Python，1 skip；292 C checks）；`make kernel` 以 `W=1` 构建所有模块，无警告。构建/测试仅离线。

## 下一步

推进完整真实提交链：先为 DDK2 render context 建立有生命周期的 backend 对象，再验证并复制 8 个嵌套数组，解析 sync/PMR handles 与访问 flags，最后把 UMD `submission_va/size/id` 接到能执行该 CCB 的 TA/3D 路径并正确返回 completion。wire struct 已固定，但在这些环节闭合前 dispatcher 必须保持拒绝 `0x82:0x14`。真实 CCB 活体验证继续需要用户明确批准。
