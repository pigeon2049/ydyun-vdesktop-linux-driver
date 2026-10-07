# r278：space 缩小绕行（离线实现，零硬件触碰）——64 页 + 256KB scratch

- **结论**：bind 黑盒打不开（r277），换 scratch 分块绕行第一步：space 2112→64 页（旧上限内，r266 验证过的量级），scratch 8MB→256KB；fire 分块循环留待下轮（本轮只缩空间 + 门禁改判）。门禁改判（space/scratch/上限断言）；`check-offline` 351 Python OK；`make kernel` 零警告。**未加载（Chrome 被动持有 renderD128，ref 1，rmmod 会拒；r120 先例：确认 passive 后停手），会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明；未执行任何 rmmod/insmod（Chrome 持有在先，`lsof` 实锤被动 open）。
2. 代码：addr 3 宏回退；门禁 3 处改判。
3. `make kernel` 零警告；`check-offline` 全绿。

## 边界

- fire 分块循环未实现；slices/fire 代码仍是 8MB 假设（`bytes > SCRATCH` 检查在 256KB 下仍过，大 rect 需分块逻辑，下轮）。
- 本轮纯离线；Chrome 持有是环境事实，不是缺陷。

## 下一步（候选，需批准）

1. fire 分块循环（离线实现+门禁）。
2. 上机验证（等 Chrome 放手或下轮）：64 页 prepare + 分块 fired/verified。
