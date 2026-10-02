# r60：file-arena 落桥并全链路验证

r55 的形状结论（arena + per-page cover）第一步落地：PMR backing 改成
file-arena sub-分配。本轮只换 backing，不碰 plan 绑定策略（未对齐仍
`-EOPNOTSUPP` 降级）——两步独立验证。

## 实现（`mt_pvr_bridge.c`）

- 每文件 2 MiB lazy arena（512 页），first-fit + 相邻合并；
  `pmr->host` 恒指首字节，mmap/DMA/plan 路径零改动。
- 超大请求先按全精度计数判定，永不截断进小槽；arena 缺失/装不下走
  私有 vzalloc（行为与旧代码逐字节一致），`arena_fallbacks` 计数。
- 重用槽 `memset` 清零（vzalloc 语义）；`mmap-out` 的 unref 包进
  `file->lock`（arena 链表的唯一无锁释放点）；`file_release` 先收 PMR
  再 `vfree` arena，附 `high_water/fallbacks` 关账行。

## 验证（新构建 `54664516`，loaded == 在盘）

- 216 项 Python（含 9 项 arena 新门禁 + mmap 非嵌套断言重写）、
  runtime integration、`W=1` 零警告全过。
- 热换（旧 bridge 引用 0、无 render fd 占用）后：smoke（引用 `1→2→1`）、
  节点探针 0 失败、UMD 八级阶梯全部 `exit=0`。
- 关账行：各文件 `high_water` 最高 99/512 页，**`fallbacks=0` 全部文件**——
  每个 PMR 都进了 arena，无旁路；零 WARN/BUG/Oops。
- 终态：`Guest/FW 2/2 pinned`、`pending=0`；主模块引用 1，bridge 引用 0。

## 边界

cover-page 绑定还没动：未对齐 range 仍进不了 plan（与 r55 结论一致）。
下一步才把 plan 绑定改成 cover 集——arena 已在位，这是前置条件。
回滚点：`/tmp/opencode/bridge-rollback-r60/`（`e812d938` 构建）。
