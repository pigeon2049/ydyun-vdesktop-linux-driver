# r261：bring-up 补 pool slices（离线实现，零硬件触碰）——TQX 发射就绪第一步

- **结论**：TQX 真发射第一步离线落地：bring-up 在 flavor-1 context 创建后调一次 copy 版 `mt_tqx_work_prepare_from_pools` 填充 `tqx_pool_slices[3]`，work 随即 cancel（slices 留 context），fill 版复用。非致命（DM bring-up 不受影响，fire 查 `tqx_slices_ready`）。附带 `pvr_translator_bo_read`（upload ops 读镜像）与 teardown 的 slices 先释放后 destroy。门禁 6 项（含反向：删 slices 调度即红）；`check-offline` 333 Python + 292 C 全绿；`make kernel` W=1 零警告。**未加载（在载桥仍 r222 构建），会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明零硬件触碰：全程只改源码 + 跑离线门禁；`lsmod` 开工收工一致（1/0）。
2. 代码：translator 结构体 `+tqx_slices_ready`；`pvr_translator_bo_read`（镜像 write）；`pvr_translator_tqx_slices`（topology→临时 src/dst BO→workspace→upload ops→buffers 锁→prepare→cancel→标志+dmesg；临时 BO 的 VM 绑定由 space destroy 兜底，注释在位）；bring-up 接入；teardown 先 release 后 destroy；两头文件 include。
3. 修 3 个编译错（slices 前向声明、`->lock` 取址多余、门禁 `cls/self` 笔误），过程如实记录。
4. 反向验证：删 slices 调度 → 门禁 FAIL；还原 → OK。
5. `make kernel`（全模块）零警告；`check-offline` 333+292 全绿。

## 边界

- slices 是否真就绪待活体（`tqx slices: ready` dmesg 行）；fire 函数仍缺（下轮）；执行未碰。
- file-static upload dev 指针仅 slices 期间有效（translator_lock 下串行，注释在位）。
- requirements.json 生成器产物未动（实现轮同步改，r190 口径）。

## 下一步（候选，需批准）

1. slices 活体验证（批准执行）：`=2` + tqx_ctx 重载 + 真实 blit → `tqx slices: ready` 行。
2. fire 函数（离线实现+门禁）。
3. 活体 fired/verified（批准执行）。
