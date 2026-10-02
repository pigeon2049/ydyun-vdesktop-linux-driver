# r46：重启后新 `mt_guest_probe` 首绑与 DMA mask 核验

经用户授权，在重启清除旧 pinned 会话后进行。未启动固件试连接或 GPU 工作。

## 前置恢复

- 重启后 `mtgpu` 自动占有 `0000:00:0e.0`，但内核日志确认
  `PhysHeapsInit` 失败（`[644]` / `-19`），无 S3000 DRM 节点；显示仍由
  QXL `card0` 提供，`mtgpu` 引用计数为 0。
- 解绑该失败的 `mtgpu` 后 PCI BusMaster 为关闭状态。
- `mt-status --read-status` 显示残留 `Guest=2/FW=1`。先以
  `mt_cold_disconnect finish=0` 只读验证：固件 `started=0`、全部 18 个
  queue ring `head==tail`，result=0；再以 `finish=1` 发布 Guest OFF。
  helper 回读和独立 `mt-status` 均确认 `Guest=0/FW=1`，BusMaster 仍关闭。

## 首绑验证

从本工作区加载 `kernel/mt_guest_probe.ko`，参数仅为
`enable_probe=1 query_info=1`（无 `probe_rpc`、`load_firmware`、
`trial_connect`、`runtime_context`）。候选 SHA-256 与
`runtime-integration-build.json` 完全一致：
`2b53f1b3f6074c2ed44ec6ad571e5e4b44398174065a84880bd5f85b1204329d`。

结果：

- `0000:00:0e.0` 成功绑定 `mt_guest_probe`；`dma_mask_bits=40`。
- 信息页响应有效：magic `aa557491`、version 2、OSID 1。
- Guest/FW 保持 `0/1`，PCI `BusMaster-`；没有固件上传、共享 channel
  注册、试连接或 GPU 工作。
- sysfs：`trial pinned=0, connected=0`；`runtime prepared=0,
  published=0`；buffers/VM/process/context 均为 0。
- 设备没有 render node，桌面仍由 QXL 提供。

首绑路径中的显式 40-bit DMA mask 已在重启后的新模块上验证。当前新主模块
保持绑定在 query-only 状态；这不代表已连接固件或启用 GPU 加速，也没有验证
新的完整 trial/session。
