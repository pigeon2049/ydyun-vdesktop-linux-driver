# r58：全轮复核（代码重读 + 活态核对）

受 `PROGRESS-SNAPSHOT` §12 过期触发（仍写 bridge 未加载，实际已加载并跑完
L3/L4），把 DMA/VM/测试/报告/活会话逐项重读一遍。结论：1 个真实 bug，
2 处注释/清理，1 起已结束的桌面侧软锁死事件；其余全部成立。

## 1. 真实 bug：`dma_source_release()` 解错地址域（已修）

GPA 窗口修正把 `page_pa` 换成 GPU PA 后，`dma_source_release()` 仍用
`page_pa` 做 `dma_unmap_page`——会把从未 map 过的 GPU PA 交给 DMA API，
并泄漏 `dma_source_dma_addrs`/`dma_source_address`。该路径只在
init-失败时走（成功路径 retained 不调用；r49 成功前未触发），故从未开火。
已改成与 `fail:` 标签逐行一致，并加回归测试
（`test_release_unmaps_dma_iovas_not_gpu_pas`：断言 unmap 用 dma_addrs、
释放指针置空）。`run_set` 的 sync 循环用对了数组，无需改。

## 2. 误导注释：锁顺序（已修）

bridge 注释称 `device_lock -> trial_lock` “与 probe/remove 加锁顺序一致”，
但 probe/remove 根本不拿 device_lock。全树反向扫描确认真正的 invariant：
没有任何路径在持有 trial_lock 时拿 device_lock，嵌套只有
device→trial 一个方向——无 ABBA 可能。注释已按此重写。

## 3. 小清理

- `pvr_cmd_pmr_map` 的 `res` 内外层 shadowing 去掉（行为不变）。
- `pvr_fence_poll` 处记录 always-ready 的用户态忙循环风险：本轮真机期间
  一个桌面 `gmain` 线程软锁死约 1300 秒后消失（01:33–01:56），无 D 态残留，
  无我方模块 WARN，会话全程健康。时序上与 bridge 节点存在期重叠
  （Chrome 确实打开过 renderD128），但无因果证据，记为 unattributed
  桌面事件。ready 语义必须保留（S4-1 UMD 等 fence），仅注释不断言。
  若复发：先抓 spinning 线程用户态栈再杀。

## 4. 逐项成立（抽查）

- 注册/释放 unwind：部分映射失败回滚页数正确；`dma_npages` 只在成功时置位；
  `dma_pdev` 引用配对；`pvr_file_release` 先 unbind 再 destroy 再 unref。
- VM 计划：ensure 失败回滚引用正确；`pvr_pmr_put -EBUSY` 只拦 mapped（ladder
  走 unmap-then-unref，不触发）；file 结构 kzalloc，新增字段零初值。
- `mt_ce_copy` scatter 分支逐项验算无 OOB（页索引上界、溢出守卫成立），
  最坏 ~1M 次循环有界。
- `dma_source_bo_create` 的 store 锁即 trial_lock（init 时同一把），
  `borrowed` 跳过 `mt_vram_free`，`module_put` 配对正确。
- r53 的构建对应关系已过期（bridge 又重编两次），但“运行中仍是已验证构建”
  依然成立：loaded `c77ad92d…` 未动；在盘 `e812d938…`（本次复核修复，
  行为仅差未加载 helper 的 release 路径）——**未重载**。

## 5. 门禁与活态

207 项 Python、runtime integration（含主模块 `W=1` + ABI）、recovery
`W=1` 全过。会话 `Guest/FW 2/2 pinned`、`pending=0`；主模块引用 1，
bridge 引用 0。`PROGRESS-SNAPSHOT` §12 已按现状刷新（标活页）。
