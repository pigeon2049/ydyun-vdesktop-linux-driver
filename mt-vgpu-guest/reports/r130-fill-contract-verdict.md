# r130：out_syncobj=0 契约裁决——测试过期，修正后 smoke 全绿

r129 的红灯已闭环。结论：**测试过期，驱动正确**；
修正测试后 smoke 全绿，新会话迎来首个绘制像素
（16×16 全面 `0xff123456`，逐字节 + 护卫字节 + 跨 GEM 三重验证）。

## 裁决（离线证据）

- 驱动三路径一致：copy/fill/submit 均为 `if (r->out_syncobj)`，
  0 = 不用 syncobj；`!r->out_syncobj` 强制要求在 git 历史中**从未存在**。
- `if (r->out_syncobj)` 自 r40（5359c45）即是接口原设计；
  测试的 EINVAL 期望随初始导入提交（6e4ebd0）到来，
  从未经活体验证——day-one 误期，不是回归。
- 旁证：兄弟工具 `mt-drm-check` 只测 `0xffffffff`（坏句柄），
  从未要求 0→EINVAL；uapi 注释 submit 侧明写 Optional。
- 故：改测试，不改驱动。uapi fill 注释补 Optional 说明（注释 only，
  零功能影响）。

## 修正（含牙的放宽，非撤门）

- `userspace/mt-fill-check.c`：删 bad[3]（14→13），新增
  `optional_syncobj_fill()`——0-syncobj fill 必须执行、返回非零
  fence seq、completed 恰 +1、像素逐字节正确，否则失败；
  `expected[]/count` 记账保持，后续全表面/copy/终局断言原样成立。
- 反向验证：r129（旧期望）即失败证据；新断言若驱动拒 0-syncobj，
  `REQUIRE(ret==0)` 当场失败——双向都有失败路径覆盖。

## 活体验收（实测）

- `mt-fill-check /dev/dri/renderD131 smoke`（新编二进制，-Werror 干净）：
  `invalid_cases:13` 全拒；`optional_syncobj_fill sequence=2`；
  `native_fill 16×16 color=0xff123456` 三重验证过；
  `fill_to_copy:true`；终局 `submitted=completed=3, faulted=0`。
- 附带观察（既有行为，非新问题）：fence seqno 比 submitted 大 1
  （r128 同模式：1 submit → last_seq 2；本轮 3 submits → sequence 4）。
- 卸载干净（同签名 WARN 累计 8 条，`grep -vc` 确认无其他签名）；
  probe 引用 87→113（又是 +26；r127 的 +34 仍是孤例）；
  桥探针全绿，freeze 继续。

## 未做

- push：67 提交未 push（用户此前明确暂不 push）。
