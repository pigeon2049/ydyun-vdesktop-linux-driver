# r268：space 上限修复（离线实现，零硬件触碰）——2112 页 + 栈改堆

- **结论**：r267 fire 活体失败（prepare `-EINVAL`）的根因离线定位并修复：`MT_BOOT_MAX_TABLE_PAGES 64U` 拒绝 2112 页 translator space（`vm_vram_create` 上限检查）。改上限 64→2112（注释构成）+ `mt_mmu_build_pages` 的 `keys[]` 栈数组（8.4KB，`-Wframe-larger-than`）改堆分配（kvzalloc/kvfree，goto-out 重构 9 个 return，零语义差）。门禁：fire 新增上限断言（含反向：改回 64 即红）；`check-offline` 344 Python + 292 C 全绿；`make kernel` 零警告。**未加载，会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明；`lsmod` 开工收工一致。
2. 活体失败复盘（r267 轮遗留）：`prepare failed: -22` → `vm_vram_create` 的 `pages > MT_BOOT_MAX_TABLE_PAGES` 上限检查（执行级定位：源码行 + 活体错误码互证）。
3. 代码：上限 2112 + 注释；keys 堆分配重构。
4. 反向验证：上限改回 64 → fire 门禁 FAIL；还原 → OK；重编零警告。
5. `make kernel` 全模块零警告；`check-offline` 全绿。

## 边界

- fire 活体仍待验证（批准执行）；小 space 调用者不受影响（上限只放宽）。
- requirements.json 未动。

## 下一步（候选，需批准）

1. 活体 fired/verified 重试（批准执行）：`=2` + tqx_ctx + fire 三开 + 真实 blit。
