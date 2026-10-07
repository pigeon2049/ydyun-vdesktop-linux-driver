# r267：fire 函数离线实现（零硬件触碰）——submit + workqueue 异步回读

- **结论**：TQX 真发射第二步离线落地：`MT_TQX_SCRATCH`（8MB @ `0x41000000`，space 扩大 32→2112 页）+ dry-run 抽 `locate_dst` helper（digest 逐位不变）+ `translate_tqx_fire` 参数门 + `pvr_submit3_transfer_fire`（slices 就绪检查 + 单飞 fence 复用 + fill prepare + submit + schedule work）+ `pvr_translator_fire_work`（等 fence → 读 scratch 全比对 → verified 行；只碰 translator 内存；teardown 首 cancel）。门禁：fire 8 项 + dry-run 改判 + addr 改判（含反向：掐门控即红）；`check-offline` 343 Python + 292 C 全绿；`make kernel` 零警告。**未加载，会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明；`lsmod` 开工收工一致（probe ref 1 / bridge 默认 ref 0）。
2. 代码：addr 3 宏 + space 扩大；translator 状态 7 字段；bring-up scratch 绑定 + teardown put；locate helper；fire + work；handler 接线；teardown 首 cancel。
3. 修 5 处（fire 前向声明/fill_work 头文件/dry-run 门禁指向/helper 重复断言/addr 括号值改纯字面量），如实记录。
4. 反向验证：掐 fire 门控 → FAIL；还原 → OK。
5. dry-run digest 不变性：fi/prog/打印格式逐项一致（活体复验下轮）。

## 边界

- fire 正确性待活体（fired/verified 行）；UMD 池写回不在本轮（work 只读 scratch，r267 设计）。
- work 无锁读 translator 字段（单飞 + cancel 保护，注释在位）；pending 语义：fence signaled 才允许新 fire，否则 EBUSY。
- requirements.json 未动。

## 下一步（候选，需批准）

1. 活体 fired/verified（批准执行）：`=2` + tqx_ctx + fire 重载 + 真实 blit → fired + verified 行。
