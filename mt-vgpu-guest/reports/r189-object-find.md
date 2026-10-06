# r189：对象查找去重 + 二次审计结论（零硬件触碰）

- **结论**：`pvr_object_find(file, handle, kind)` 收敛 9 处重复
  （7 destroy + submit 查找 + reservation 包装），并删去 map 内
  在锁内重复的 reservation 查找（注释论证复用安全）。门禁 295+292
  全绿，`W=1` 零警告；反向验证通过。本轮另走查 connect/event/
  multicore/info/heap/pmr/open/postclose/arena——结论均为不动
  （见下）。

## 实测

1. 去重：新增 helper（handle+kind 匹配，NULL 兜底）；
   `pvr_reservation_find` 收为包装；7 destroy + submit 改调 helper
   （`list_del`/`kfree`/`-ENOENT` 语义逐位不变）；map 删第二查找。
   bridge 净 -11 行。
2. 门禁：新增 `test_pvr_object_find.py` 3 项（helper 语义、
   8 函数调 helper 且无线内循环、map 仅一次查找）；
   tdm_context2 旧断言同步到 helper 形式（意图不变）。
   全量 295 Python + 292 C 全绿。
3. 反向：一处 destroy 还原内联循环即红，还原 helper 即绿。

## 二次审计结论（不动项）

- connect/event/multicore/info/heap/pmr/ctx handlers：注释完备，
  无重复可抽。
- arena/postclose/mmap 引用计数：r151 已定型，不动。
- `pvr_cmd_kicksyncctx2_create`/`render2_create` 的 `(void)in`：
  IN 载荷对桥不透明是设计，不是缺口。
- dispatch 缩进参差（内层 `switch` 多一 tab）：纯格式，
  无门禁可钉， Vet 不碰（churn 无收益）。

## 边界

- 纯结构重构，查找语义逐位一致（首匹配 + NULL），无行为变化；
  未碰模块加载与会话。

## 下一步（候选）

- 活体项仍待批：kill-while-busy 关账 / TQX 真发射 / `=2` update 验证。
