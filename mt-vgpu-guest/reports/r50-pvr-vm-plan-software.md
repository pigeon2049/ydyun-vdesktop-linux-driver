# r50：PVR PMR GPU-PA 页表输入计划（软件侧）

在 r49 的 TQX GPU 回读成功后，继续接入 PVR `MapPMR` 的页表输入路径。

## 实现

- 每个 PVR PMR 分别保存 `dma_addr`（只用于 DMA API unmap）和窗口转换后的
  `gpu_pa`；`gpu_pages[]` 提供给 VM planner。
- `pvr_file` 持有一个 32-table-page 的 CPU-only `mt_gpu_vm` plan。对齐的
  `MapPMR` 调用 `mt_gpu_vm_bind()`；byte-unaligned/超范围 PMR 仍保持原 UMD
  返回语义，但不伪造 page-table binding。
- `UnmapPMR` 先撤销 plan binding；mapped PMR 的 `PmrUnref` 返回 `-EBUSY`。
  文件关闭时按 binding unbind、VM fini、PMR unref 顺序清理。
- 计划只构造 CPU 页表 image：**不 upload、不 seal、不发布 root、不提交工作**。
  GPU PA 使用 `mt_system_page_address()`，不把 DMA IOVA 直接当 PTE 地址。

## 验证与当前限制

- recovery bridge `W=1` 构建通过；全套 **196 项 Python 测试**通过，
  `git diff --check` 通过；runtime integration 仍通过，主模块散列
  `52c89e90785495569d4b4b116a5f60e728a4ca89f1941ab0ff45469fd6991049`。
- 新 bridge 候选 build-id `c77ad92d33bc5e58787fc3dbab30d23dce7cb2d0`，
  SHA-256 `c8afb8a421648837860d59662db207cb46924dd007f755f95e750760cb8caa5a`；
  **尚未加载**。运行中的 bridge 仍是旧 build，Chrome 持有其 render fd
  （bridge refcount 1）；主模块和 TQX 实验也 pinned（分别 refcount 17、1）。
- 因此本轮只验证代码构建和软件测试，尚未验证 live PVR MapPMR 创建/撤销该
  VM plan；尚未 upload root、执行 PVR UMD 或 GPU kick。

下一次 live 验证需要安全释放当前 pinned session/Chrome bridge fd 后，加载新
bridge，以对齐的四页 PMR 检查 `gpu_plan_result` 并确认 Unmap/File-close 清理。
