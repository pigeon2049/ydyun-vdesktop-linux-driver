# r220：SyncPrimSet 由 stub 改真写（离线实现，零硬件触碰）——值语义链卡点打通

- **结论**：本轮“打通卡点”落到实处：`0x2:0x2` SyncPrimSet 从 `pvr_stub_ok` 改为真写 `pvr_cmd_syncprim_set`。ABI 三重互证一致——同 SHA UMD wrapper `FUN_00139220`（IN `{u64,u32,u32}` 16B）、生成头 `MTGPU_BRIDGE_IN_SYNCPRIMSET`（hSyncHandle@0/ui32Index@8/ui32Value@12，OUT eError）、活体 r145 尺寸（16/4）。handler 复用 translator 解析（PMR 直柄或 SYNC 对象跟随 backing 链，其余拒绝）+ `index*4` 防溢出定界 + PMR host 写 u32 + 零填充 OUT；无 fence、无提交、无 wakeup（waiter 轮询语义天然无丢失唤醒）。`0x2:0xd`（DDK2 CpuSignal 变体）仍越界，越界声明。门禁：新文件 7 项 + `ddk2_render2` 改判 + wire MAPPING 补两行（含双重反向验证）；`check-offline` 317 Python + 292 C 全绿；`make kernel` W=1 零警告。**未加载（在载桥仍 r216 构建），会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明零硬件触碰：语料只按名查询（`FUN_00139220`/`FUN_001a0a50`/`FUN_00139360` 三段伪 C，不通读；SHA `b3058c02…` 与 DECOMPILATION.md 已对版）；`lsmod` 开工收工一致（1/0）。
2. 代码：`mt_pvr_wire.h`（两 struct + 两 static_assert）与 `mt_pvr_bridge.c`（handler + dispatch 改一行，`SYNCPRIMSET` 从 stub 组摘出）。
3. 门禁镜像既有惯例：路由/ABI/解析复用/定界/仅 host 写/零执行 token/零填充回 0；`test_sync_prim_set_stubbed` 按 r174 先例改判为 `test_sync_prim_set_writes`。
4. 反向验证两轮：① dispatch 改回 stub → 新门禁 + 改判门禁双红，还原即绿；② 删 `!gfx_out.error` 类笔误式改法（上一轮教训：先确认改法真能触发）——本轮直接改 dispatch 行，一次命中。
5. wire MAPPING：补 `(0x2,0x2)` 两行后，生成表 16/4 diff 通过（第四重互证落定）。

## 边界

- 只实现值写入；wait 路径（5s 轮询）未动；非零值 kick 活体（preset → match → marker → fence）待下轮批准窗口（重载 + raw set + kick 链）。
- DDK2 `0x2:0xd` 未实现仍 `-ENOTTY`；UMD 的 `SetSyncPrim` 导出封装未走（raw 先行，faithful 封装待活体轮对比）。

## 下一步（候选，需批准）

- 非零值翻译 kick 活体：重载新构建（默认 + `translate_kick=1` 需两次窗口或一次双参）→ raw `0x2:0x2` 置值 → r73 链非零 check → `translated kick` + fence。
