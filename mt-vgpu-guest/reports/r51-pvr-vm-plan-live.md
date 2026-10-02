# r51：PVR PMR CPU-only VM plan 真机验证

在用户授权的重启后完成。`mtgpu` 仍在 `PhysHeapsInit [644]` 失败，按既有流程只读验证
idle rings，再由 `mt_cold_disconnect finish=1` 将 Guest 从 2 置为 0，随后加载新主模块
并建立 runtime-context trial（OSID 1，Guest/FW `2/2`，`pending=0`）。

## 加载的二进制

- 主模块 `mt_guest_probe.ko` SHA-256
  `52c89e90785495569d4b4b116a5f60e728a4ca89f1941ab0ff45469fd6991049`，
  build-id `4f843a1341037c0c7125cde6969738198cb29215`，与候选文件一致。
- 新 bridge `mt_pvr_bridge.ko` SHA-256
  `c8afb8a421648837860d59662db207cb46924dd007f755f95e750760cb8caa5a`，
  build-id `c77ad92d33bc5e58787fc3dbab30d23dce7cb2d0`，与候选文件一致。
- trial 证据：`build/fresh-trials/20261002T172500Z-21afe996/result.json`
 （`connect_result=0, connected=1, published=1, pinned=1`）。

## 验证内容

1. 四页（16 KiB）system PMR 的 `pvr_dma_smoke`（root 执行）通过：
   `map returned mapping=0x1000, session refs 1 -> 2`，
   释放后回到 `1`。未提交 GPU 工作。
2. 内核日志确认双地址域与 VM plan：
   - `DMA domains: dma_iova=0x18d817000 gpu_pa=0x898d817000 pages=4`
   - `CPU-only PVR VM plan root=0xfffffe0000 va=0x5000000000 bytes=16384 pa=0x898d817000`
3. `pvr_node_probe /dev/dri/renderD128 1` 全部通过（0 failing step、0 mismatch）。
4. 日志无 DMA-API、WARN、BUG 或 Oops；会话保持 Guest/FW `2/2`、
   `pending=0/completed=0`；主模块引用回到 1，bridge 引用为 0。

## 边界

- 该 VM plan 是 CPU-only：只构造页表 image，**未 upload root、未 seal、
  未发布、未执行**，也没有驱动真实 MUSA UMD kick。
- PMR 释放后引用配对恢复；mapped PMR 的 `PmrUnref` 按新逻辑拒绝 `-EBUSY`
  直到 UMD unmap（本轮 smoke 走了完整 unmap/unreserve/unref 路径）。
- 主模块仍 pinned（refcount 1），BusMaster 开启；不要卸载、解绑、
  disconnect 或提交额外工作。
