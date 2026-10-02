# r55：byte-tight PMR 的页表答案（arena + per-page 绑定）

承接 r54（9/12 PMR 因未对齐进不了 VM plan）。先证明 naive round-down 是错的，
再给出经页表走读验证的正确形状。全程离线 RAM 测试，零硬件触碰。

## 危险：round-down 会 double-map

r54 清单中的相邻两项 `[0x8000010000+0x253]` 与 `[0x8000010253+0x408f]`，
按页取整后共享 VA 页 `0x8000010000`。RAM 复现：第一个绑定成功后，
第二个取整绑定被 `mt_gpu_vm_bind_many` 以 `-EEXIST` 拒绝——若强行压入，
同一 VA 页将同时指向两个 PMR 的 backing，GPU 读到错字节。这是当前
bridge 对未对齐返回 `-EOPNOTSUPP` 而不是静默取整的原因（已由
`test_byte_tight_pmr_degrades_from_gpu_plan` 钉住）。

## 答案：arena backing + per-page 绑定，planner 零改动

12 个真实 range 的并集 cover 共 76 页。单个 arena BO 背下全部 cover 页，
每个 VA 页一条 4 KiB binding（`{arena, va_page, arena_offset, 4096}`），
共享页只出现一次、同时承载相邻两 PMR 的 sub-page 字节——这正是
per-PMR vzalloc 表达不了、而 UMD arena 语义天然满足的。
`tests/pvr_arena_plan_test.c` 用独立三级走读逐字节核对全部
302,227 字节（= 清单总量 0x49c93），与 planner 零耦合改动。

## 含义与下一步

- 翻译器要的页表输入不需要新 planner：bridge 侧把 PMR 改成 file-arena
  sub-分配（mmap/DMA 路径兼容：同页取 `vmalloc_to_page(arena+off)`），
  plan 侧按 cover 集逐页绑定。arena 分配器本身是下一个独立工作项，
  本轮只定形状。
- 验证：202 项 Python（含新增降级测试）、runtime integration（含新增
  arena C 测试 + 主模块 `W=1` + ABI）全过。会话 `Guest/FW 2/2`、
  `pending=0`，无 WARN/Oops。
