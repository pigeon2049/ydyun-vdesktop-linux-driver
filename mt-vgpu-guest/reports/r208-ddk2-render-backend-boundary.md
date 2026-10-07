# r208：PMR 页可借入设备 BO store，但真实 TA/3D 后端仍缺 per-file VM 与资源闭包

## 范围与结论

零硬件触碰。本轮只读检查 `mt_bo`、GPU VM、PVR PMR 和现有执行上下文代码，没有加载/卸载模块、提交 GPU 工作或改动运行态。

`mt_bo_system_borrow()` 是把 PVR 页接入真实设备 BO store 的可复用入口：它接受 `mt_system_memory` 的 CPU 地址和已翻译 GPU page vector，构造的 BO 与设备 session 使用相同 store/ops；`mt_gpu_vm_bind_many()` 因而可接受它，并能保留原 PMR 页地址。它要求 BO 所引用的 memory descriptor 生命周期不短于 borrowed BO。当前 `pvr_pmr` 尚无此 descriptor，现用的 `gpu_bo` 是 `store=file`、`pvr_gpu_plan_bo_ops` 的 CPU-only 规划 facade，不能进入设备 session VM。

即使逐 PMR 改用 borrowed BO，也还不能接入当前 translator：translator 的真实 VM/context 是全局共享对象，而 PVR reservation/PMR VA 台账是 per-file；共享 VM 会造成地址空间所有权冲突，且 render context 仍只是 handle token。真实路径需要 per-file、由设备 buffer store 支持并上传的 GPU VM、对应 execution process/render context，以及 CCB 所引用 PMR 在该 VM 的绑定和存活引用。还需解析/验证嵌套 sync/PMR 数组与 CCB 资源引用，建立 TA/3D 包格式和完成语义；不得将当前空 marker 当作用户 CCB 的执行结果。

## 实测代码事实

- `mt_bo_system_borrow()` 在 `kernel/mt_bo_vram.h` 保存 `struct mt_system_memory *` 于 borrowed BO handle，并以其 `page_pa` 建立非连续物理页向量；BO `store` 是传入的设备 `mt_bo_store`。
- `mt_gpu_vm_bind_many()` 在 `kernel/mt_gpu_vm.h` 要求 BO 与页表 BO 的 `store` 和 `ops` 指针完全相同，校验区间后才对 BO 取引用并更新页表规划。
- PVR PMR 页由 `pvr_pmr_dma_register()` 映射；`pmr->host` 来自文件 arena 槽或 `vzalloc`，`pmr->gpu_pages` 为逐页 GPA。PMR 释放负责 DMA unmap、页向量释放和 host/arena 释放。
- 当前 PVR per-file GPU VM 的页表 BO 和 PMR facade 都标记 `store=file`、ops=`pvr_gpu_plan_bo_ops`；该 ops 只有 free no-op，VM 注释明确是 unpublished CPU-side planning domain。
- 真实 translator 持有全局 `translator.space/process/context`；`mt_execution_process_create()` 将 process 绑定给一个已初始化的 `mt_gpu_vm`。这与 PVR 的 per-file reservation/PMR 生命周期目前没有连接。
- 通用 work submit 校验 command BO 与 VM/store 的映射一致，但并不解析命令流。现有 RGX submit 仍是固定 marker；TDM Submit3 仍不执行 nested CCB。

## 可行设计方向（推断，未实现/验证）

1. 让 per-file PMR 拥有稳定的 `mt_system_memory` 描述对象，借用前确认 host 长度按页对齐、所有 DMA 映射有效，且 PMR 释放先等待 GPU 使用结束，再销毁 borrowed BO，最后解除 DMA 映射/释放内存。
2. 为每个 render context 建立设备 store 内的页表 BO、真实 VM 和 execution process/context；按 per-file reservation VA 把 PMR borrowed BO 绑定进该 VM，上传并发布页表。不得复用当前 CPU-only plan BO。
3. 在 dispatch 前验证所有嵌套数组的指针/计数/长度，关联 check/update sync 与 PMR handles；确认 CCB 和它引用的资源都完整映射到该 VM，并为 in-flight work 持有对象引用。
4. 先补离线门禁覆盖 borrowed BO 生命周期、store 拒绝、per-file VA 隔离和失败回滚；再接入有证据支持的 TA/3D packet builder 和 fence 完成路径。真实 CCB 活体验证仍须单独取得明确批准。

这些步骤只是可行路线，不能据此声称 UMD CCB 与现有静态 DM2 模板兼容，也不能声称 PMR 目前可提交执行。

## 验证边界

只读源码核对；无代码修改、无门禁执行、无硬件操作。下轮若进入代码实现，按仓库规则运行 `make -C mt-vgpu-guest check-offline`，并为新增行为进行反向验证；改内核后还需 `make kernel` 且 W=1 零警告。
