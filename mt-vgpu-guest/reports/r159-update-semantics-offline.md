# r159：update 语义的 UMD 侧确定——布局/可见性/完成条件（离线；零硬件触碰）

- **结论**：update 数组三要素已从核对语料逐项确定，与桥现有实现一致，无需改线
  布局；`flag&2` 置位来源（哪条 UMD 路径产生 update 条目）仍待活体确定，
  按 STATUS 安排留待 bridge 可重载后。
- 本轮零硬件触碰（`lsmod` 无 `mt_*`，未加载/卸载任何模块）；另顺手清除
  r148 调试期残留在桥源码中的 5 处 `DBG` 日志，重建后 `make kernel` W=1
  零警告、`make check-offline` 全绿。

## 确定项（语料：`decompiled/linux-legacy-umd-5.2.0/decompiled.c`，按名查）

1. **布局**：`SyncUtilGenerateUpdateData`（`FUN_00177360`）输出三数组
   `{ufo block, offsets, values}`，对应 84B IN 的 `update_*` 三指针
   （offsets 36/44/52）+ `client_update_count`（u32@60），与
   `mt_pvr_kicksync3_in` 头定义一致；64 条钳制与生成器限位同构（超限返 3）。
2. **可见性**：update 条目 = 同步表内 `flag&2` 者（check 为 `flag&1`，r114），
   句柄/偏移经 `SyncPrimLocalGetHandleAndOffset` 得出：
   句柄 = sync block 的句柄字段，偏移 = block 相对偏移——正是桥
   `SYNC对象→backing PMR 跟随 + offset` 模型覆盖的两种形状
   （PMR 直柄与 SYNC 对象柄）；update 值取自表项 `[5]`，由 UMD 侧计算，
   桥只需原文应用。
3. **完成条件**：UMD 经 `sync_wait`（`poll(-1)`/`poll(timeout)`，`FUN_001a2d50/40`）
   等 `update_fence_fd`；桥在 fence fd 交出前已写回 update 值（先写回后交 fd），
   可见性先于完成。update-only（`ncheck==0`）跳过等待合法。

## 未定项（需活体，当前红线禁重载）

- 哪条 UMD 调用产生 `flag&2` 条目（fabricated 阶梯的 `CreateSyncPrim` 只产生
  check 侧；`0x1b0` count poke 不触发 update 数组编组，r148 已实测）。
  候选：TA/3D 提交路径或显式 update-sync 创建，待真实绘制流量确定。
- 附带：r148 遗留的 `conds[i].expected = vals[i]` 缺失已补（门禁
  `test_check_expected_values_recorded`），调试日志已清。

## 门禁

- `make kernel` W=1 零警告/零错误；`make check-offline` 全绿；
  `test_pvr_translator.py` 4 项全过（含反向）。
