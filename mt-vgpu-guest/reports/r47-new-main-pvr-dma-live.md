# r47：新主模块 retained trial + PVR DMA handoff 真机验证

经用户授权，在重启后 Guest `0/1` 的空闲设备上执行。主模块使用本轮
runtime-integration 构建；没有提交 GPU 工作。

## 建立新主模块会话

`fresh-trial.py --run --runtime-context` 预检通过：设备未绑定、Guest `0`、
FW `1`，PCI/固件身份和模块散列匹配。脚本以 `enable_probe=1 query_info=1
probe_rpc=1 reserve_memory=1 prepare_resources=1 load_firmware=1
trial_connect=1 runtime_context=1` 加载模块并留存证据：

- 模块 SHA-256：
  `2b53f1b3f6074c2ed44ec6ad571e5e4b44398174065a84880bd5f85b1204329d`。
- `insmod` 返回 0；trial `result=0, connect_result=0, connected=1,
  published=1, pinned=1, registered=15`。`disconnect_result=-61` 是该
  retained 模式的预期值，没有请求 disconnect。
- runtime context `prepared=1, published=1, result=0`；当前 Guest/FW
  `2/2`、`event_result=0`。
- 固件与启动资源已上传/发布；PCI BusMaster 已按 trial 要求开启。没有发出
  GPU work，当前 `pending=0, completed=0`。
- 完整 preflight 和 sysfs 快照：
  `build/fresh-trials/20261002T142935Z-8bcee8ec/result.json`。

## 新 bridge DMA 测试

加载当前 `kernel/recovery/mt_pvr_bridge.ko`：build-id
`5260120cbb386e0a9a619aa0d6d24a6f51fd7ca3`，SHA-256
`d1ccf20cde8f901c33d5dad9a57ae66484a3abece451f703c32352368c91015b`。
`pvr_dma_smoke /dev/dri/renderD128` 对一个四页（16 KiB）system PMR 执行
Reserve → Map → Unmap → Unreserve → PmrUnref：

```text
map returned mapping=0x1000, session refs 1 -> 2
PMR released, session refs now 1 (expected 1)
PASS: four-page PMR DMA-mapped and unmapped; no GPU work submitted
```

该增减证明 bridge 从**本轮新主模块**取得 live session，并完成四个页面的
DMA map 及对应 unmap；之前的 smoke 只覆盖了 retained 的旧主模块，本轮已将
handoff 在重启后的新构建上闭环。

## 当前状态 / 边界

- 新 `mt_guest_probe` build-id `46a2cfa464967cc691a7d01b29f4588e7490a836`
  与候选文件一致；主模块 refcount 回到 1（retained session），bridge
  refcount 为 0。
- 会话仍为 Guest/FW `2/2`、pinned、`pending=0/completed=0`；BusMaster
  保持开启。不要卸载主模块、解绑设备或尝试恢复/断开此 retained 会话。
- 已验证 CPU system pages 经新主模块的 live PCI device 建立并撤销 DMA
  映射；尚未把这些 DMA 地址编入 GPU 页表，也没有让 GPU 读写该 PMR，故不
  代表 DMA 内容传输或 GPU 渲染通过。
