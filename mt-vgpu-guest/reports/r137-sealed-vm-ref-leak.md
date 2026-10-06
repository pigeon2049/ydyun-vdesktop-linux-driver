# r137：+26/周期根因 = sealed VM 的 destroy 必败，25 个 BO backing 永不释放

- **结论**：`mt_gpu_vm_fini`（`mt_gpu_vm.h:309`）首行即 `if (vm->sealed …) return -EBUSY`，
  而 `prepare_context` 把两个 space 都 seal 了（`mt_live_3d_drm.c:747,850`），且全树**没有 unseal 路径**
  （grep `unseal` 零命中）。于是 `release_unpublished → destroy → fini` 每次必回 `-EBUSY`——
  这正是 r127 记的“2 条 sealed-VM WARN”（两个 space 各一条）。
  fini 失败后 VM 拥有的全部 BO 引用不断不断开 → `mt_bo_vram_free` 永不到达 →
  配对的 `module_put(THIS_MODULE)`（`:82`，alloc 侧 `:50` 每次 `__module_get`）永不执行 →
  **probe 引用数每周期净 +25**（另有 Δ1 未归属，见下）。
- 本轮零硬件触碰：纯静态读码 + 算术对账，未加载/卸载任何模块，未改代码。

## 算术对账（静态计数 vs 实测引用数）

- 每周期新建、且 free 永不到达的 backing（`__module_get` 各一次）：
  private 3（command_2d/dma_2d/state_2d）+ slots 8 + context_bos 11 + command_3d 1
  + 两个 space 的 tables 各 1 = **25**。
- 首周期 r127 实测 **+34** = 25 + 9：9 恰为 `MT_BOOT_BO_COUNT`（= 6 shared + 3 pools，
  `mt_boot_bo.h:9`，`MT_PROCESS_SHARED_COUNT=6` 见 `mt_process_resources.h:10-13`）。
  首周期 `bind_boot_shared` 以 `borrow` 新建 9 个 backing 缓进 boot store（`:118-127`，
  只在首周期 `__module_get`，之后各周期复用，得一次 +9）——**一次性的 +9 与每周期 +25 相加正好是 +34**。
- 稳态 r128–r130 实测 **+26** = 25 + **Δ1 未归属**。
  execution process/context 创建路径经 grep 确认无 module 引用；
  marker fence 的 get（`mt_marker_fence.h:95,236`）由 `dma_fence` release 配对
  （`put` 在 `:58`），且 fence 若随提交数泄漏，各周期数应随提交数波动——实测稳定 +26，
  故 fence 不是主因。Δ1 需活体二分确认（见下一步），不硬凑。

## 为什么 release 的 put 全“成功”却没释放（无声泄漏，非 WARN）

- `release_unpublished` 对每个 BO 只 put 一次且**从不 unbind**；
  `mt_bo_put`（`mt_bo.h:82-98`）在 `refs > users` 时即返回 0（仅 refs--），
  VM 拥有的引用（`bind` 经 `mt_bo_get`，`mt_gpu_vm.h:239`）留存 → backing refs 恒 >0。
- 只有 destroy 能收走 VM 引用，而 destroy 被 sealed 卡死。
  所以除 2 条 destroy WARN 外**无任何报错**——引用数悄悄爬，对象存储同步占满
  （快照 §11 的 `-EBUSY` 即此上游），与“跑几小时卡死”吻合。

## 修复方向（本轮不实现，单独立项）

- 候选 A：teardown 先 unbind 全部 bindings 再 fini（VM 层已有 `unbind`，但 sealed 下同样 `-EBUSY`，
  `:267`——得一并处理 sealed）。
- 候选 B：fini 允许“已 seal 但 `active_uses==0` 且调用方保证 GPU 空闲”的销毁（语义变更，
  影响全部 20+ live 模块，按 §6 必须配门禁测试 + 反向验证 + 活体引用数回归）。
- 无论哪个，都要先有活体会话做“加载→卸载→读 refcnt”的基线二分（含 Δ1 归属：
  空载卸载 vs 带帧卸载各跑一次）。

## 下一步（需批准）

1. 重建会话（r68/r69 流程）→ 跑基线二分（Δ1 归属 + 验证本轮静态结论）。
2. 修 fini/seal 语义（改代码 + `check-offline` 221+268 + 反向验证 + 活体回归）。
3. 遗留：72+ 提交未 push；`r135-major2-ccb-create.jsonl` 未入库文件仍在工作区，未动。
