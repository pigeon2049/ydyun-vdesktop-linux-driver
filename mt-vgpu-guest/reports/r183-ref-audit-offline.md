# r183：ref 漂移离线审计——`pvr_file_release` 双 early-return 可 abandon 全部 PMR `dma_owner`（零硬件触碰）

- **结论**：+65 未解释量的首要嫌疑已离线命名：`pvr_file_release`
  的两处 early `return`（unbind 失败 / destroy 失败）在释放任一 PMR
  之前直接返回，整文件 PMR 的 `dma_owner` 模块引用（每 map +1）被
  永久 abandon——数量级恰为 map 数/轮（≈14），与失败轮 +14/+23
  同形；当前活体 `mt_guest_probe` Used by=66（=基线 1＋漂移 65）
  只读吻合。prepare 失败路径本身经代码走查是对称的（teardown +
  `module_put(owner)`），DMA 注册/释放亦逐 PMR 配平——排除这两处。
  以上为静态审计结论（推断），活体差分验证待可重载窗口（需批准）。
  本轮零代码改动、零硬件触碰。

## 实测（只读）

1. `lsmod`（sysfs 只读，未触碰模块/会话）：`mt_pvr_bridge  Used by 1`，
   `mt_guest_probe Used by 66`。66 = r182 记录的基线 1 ＋失败轮累计
   +65，漂移量仍在、无残留进程/fd 变化（功能完好、仅禁 unload，与
   r182 边界一致）。bridge 的 1 疑为外部 render 节点持有者（r136
   候选③类），未深究（深究需 lsof/复现，不属本轮）。
2. `dmesg` 在本容器内无权读取（`不允许的操作`）——回避，不做内核日志断言。
3. 门禁：本轮纯文档，`make check-offline` 未重跑（上一轮 f8b4fed
   处 282+292 全绿；工作区此后无代码改动）。

## 推断（静态审计，有行号证据；未经活体验证）

1. 首要嫌疑——`pvr_file_release`（`mt_pvr_bridge.c:855`）：
   - `:868-872` 逐 binding `pvr_gpu_vm_unbind`，任一失败即
     `WARN_ON(ret)` → `return`，此后全部 PMR（`:882-887` 的
     `pvr_pmr_unref` → `pvr_pmr_dma_release` → `module_put(dma_owner)`）
     走不到；
   - `:873-875` `pvr_gpu_vm_destroy` 失败同样 `return`，同上。
   - 每成功 DMA 注册的 PMR 恰持 +1（`pvr_pmr_dma_register:696`
     `pvr_session_acquire(&pmr->dma_owner)`，`691` 已注册直接回 0，
     不重复加），blit 轮约 14 maps → 单轮 abandon 上限 ≈14，
     与实测 +14（×3）同形；+23 首轮多出部分可为首轮额外 reservation
     叠加（未定，标为未知）。
   - destroy 失败条件真实存在：`pvr_gpu_vm_destroy:591` →
     `mt_gpu_vm_fini:327` 在 `active_uses || owners` 时回 `-EBUSY`，
     在表损坏时回 `-EUCLEAN`（`:329-337` preflight）；abort 的 UMD
    （SIGABRT/timeout 杀）在 close 时正可能命中此类状态。
2. 已排除（代码对称，无需活体）：
   - prepare 失败 `out:`（`:1935-1939`）：`teardown_locked` 释放已建
     space/Bos/contexts/process（`translator.owner` 此时恒 NULL，
     只在 `:1928` 成功路赋值，无 double-put），再 `module_put(owner)`。
     配平。
   - `pvr_translator_exit`（`:2166-2189`）：acquire 的临时 ref 与
     teardown 的 `translator.owner` put 分开配平。配平。
   - `pvr_pmr_dma_register` 失败路（`:807-813`）：`put_session` 按
     `ret && dma_owner` 回 put。配平。
3. 次要未知：bridge Used by=1 的持有者身份；+23 首轮中超 14 的部分归属。

## 边界

- 本审计未改任何释放语义（r182 红线：+65 未解释即动释放语义是大忌）。
- 修复方向（仅提案，不实施）：release 路径永不 abandon PMR——即使
  unbind/destroy 报错也继续 `pvr_pmr_unref` 全表（WARN 保留，释放继续）。
  是否实施待活体差分先确认归因。

## 下一步（候选，按序；需批准才碰硬件）

1. 活体差分（defaults legacy unwind：有 map 无 prepare 的 abort 轮，
   对照 prepare-失败轮的 ref 增量）——需 bridge 重载窗口，待批准。
2. 若归因证实，再立项修 release 语义（代码＋门禁＋反向验证＋活体回归）。
3. TQX 真发射（submit+fence+落位+像素回读）排在 ref 归因之后。
