# r45：PVR system-PMR DMA live smoke

日期：2026-10-02。经用户授权进行真机测试；仅替换了无引用的 PVR bridge，
保留了正在运行的 `mt_guest_probe` 和 GPU 会话。

## 运行前状态

- 设备：Moore Threads S3000 `0000:00:0e.0`，`1ed5:0222`，绑定
  `mt_guest_probe`，`dma_mask_bits=40`。
- 会话：`guest=2 firmware=2 started=1 connected=1 pinned=1`，
  `event_result=0`，`pending=0 completed=2`。
- `mt_guest_probe` 引用计数为 18；旧 `mt_pvr_bridge` 引用计数为 0。
- 只卸载/替换了无引用的 bridge，没有解绑 PCI、重载主模块、改写固件、
  提交 GPU 工作或触碰显示配置。新 bridge build-id：
  `5260120cbb386e0a9a619aa0d6d24a6f51fd7ca3`，SHA-256：
  `d1ccf20cde8f901c33d5dad9a57ae66484a3abece451f703c32352368c91015b`。

## 实测

1. 新 bridge 的 PVR 节点注册成功，`DRM_IOCTL_VERSION` 返回 `pvr 0.1.0`。
2. `pvr_node_probe /dev/dri/renderD128 1` 全部通过（0 failing step、
   0 mismatch），检查 Connect、15 项 heap、Info PMR mmap、sync PMR、
   reservation/ZSBuffer 生命周期及未知命令拒绝。
3. 新增的 `probe/pvr_dma_smoke.c` 创建一个 16 KiB PMR（4 个 vmalloc 页），
   执行 ReserveRange → MapPMR → UnmapPMR → UnreserveRange → PmrUnrefPmr。
   MapPMR 返回 `mapping=0x1000`；映射存活期间主模块引用计数
   `18 → 19`，释放 PMR 后恢复为 `18`。这证明 session acquisition、
   四页 DMA map 及对应 unmap 均成功；未提交 GPU 工作。

## 运行后状态

- `guest=2 firmware=2 started=1` 保持不变，`pending=0 completed=2`，
  BO/VM 计数不变。
- `mt_guest_probe` 引用计数 18，`mt_live_tqx` 1，`mt_pvr_bridge` 0；
  节点探针和 DMA smoke 都已关闭文件并释放对象。
- 近 10 分钟内核日志仅见旧 bridge 正常卸载及新 bridge 注册，未见
  DMA-API、WARN、BUG 或 Oops 报告。

本轮证明的是 CPU system pages 可通过当前 live PCI device 建立并撤销 DMA
映射；尚未验证 DMA 地址进入 GPU 页表，也没有用 GPU 读取/写入这些 PMR。
`mt_guest_probe` 当前活动二进制未替换；本轮验证限定于上述存活会话和
新 bridge build。
