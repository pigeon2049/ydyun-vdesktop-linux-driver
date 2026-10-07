# r256：TQX 真发射路径盘点（离线 recon，零硬件触碰）——bring-up 补 slices 即发射就绪

- **结论**：TQX 真发射的完整链条已离线闭合（无未知件）：live_3d_drm 是可照抄的模板（`fill_work_prepare → submit_tqx_work → fence wait → 回读`），translator 侧缺的唯一件是 flavor-1 context 的 pool slices（shader/PDS 程序池）；补法是在 bring-up 内调一次 `mt_tqx_work_prepare_from_pools`（copy 版，内部填充 slices，结果 work 可 cancel 丢弃），fill 版复用 slices。路由已对（node_type=1 → type=1/dm=1，fill_work 要求满足）。锁无障碍（Debian 无 lockdep；bring-up 持 trial_lock 与 live_3d 持 submit_lock+trial_lock 同构；pools prepare 要 buffers->lock 由调用者持，bring-up 内加即可）。destination 借用走 r208 `mt_bo_system_borrow`（PMR host 逐页 `vmalloc_to_page` + `page_to_phys` 构造 `mt_system_memory`，r151 模式现成）。回读走 PMR host（比 BO 回读更直接）。下轮立项：bring-up 补 slices（离线实现+门禁）→ fire 函数（离线实现+门禁）→ 活体（双开 blit → fired/verified）。

## 实测（语料核对，只按名查）

1. `mt_node_route_build(type=1)` → type=1/dm=1（`mt_work_command.h:99`）；`fill_work_prepare` 要求 `route.type==1 && route.dm==1`（`mt_tqx_fill_work.h`）；translator tqx_context 创建参数 `(process, 1, 0)` 与 live 路径同形。
2. live_3d_drm 发射序列（`mt_live_3d_drm.c:457-490`）：`fill_work_prepare` → `markers.ready/work_ready` → `submit_tqx_work` → fence → `dma_fence_wait_timeout(5000)` → 回读比对。
3. `mt_tqx_work_prepare_from_pools`（`mt_tqx_work.h:92`）内部从 shared pools 分配并缓存 `context->tqx_pool_slices[3]`；fill 版要求调用者已备 slices（`mt_tqx_fill_work.h`）。
4. `mt_live_tqx` 不可用（retained 污染型，快照 §11；本轮已排除）。
5. `mt_bo_system_borrow`（`mt_bo_vram.h`）要 `mt_system_memory{cpu, page_pa[], bytes}`；PMR 有 host + gpu_pages，缺 page_pa[]（CPU 物理页数组）——逐页补（r151 模式）。
6. 锁：`mt_gem.h` 包装函数自取 `buffers->lock`；pools/fill prepare 要求调用者持；bring-up 持 trial_lock，内加 buffers 锁与 live_3d 同构。

## 边界

- 伪 C/注释是路径假设，执行语义以活体为准（§9）；本轮零代码、未跑门禁、未碰会话。
- fire 函数的 dst 定位复用 dry-run 逻辑（抽 helper），`translate_tqx_fire` 参数门默认 off；bring-up slices 与 fire 分两轮落地（一次只做一件事）。

## 下一步（候选，需批准）

1. bring-up 补 pool slices（离线实现+门禁+反向）。
2. fire 函数（离线实现+门禁+反向）。
3. 活体：双开 blit → fired/verified（批准执行）。
