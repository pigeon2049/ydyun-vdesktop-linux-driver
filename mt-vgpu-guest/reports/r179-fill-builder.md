# r179：T3-transfer fill-input 构造器（纯函数 + 离线门禁；未接活）

- **结论**：新增 `kernel/mt_transfer_fill.h`（内核/用户态共享，纯整数 + memcpy）：`mt_transfer_pool_parse`（池→`{pixels, color}`，r178 代数；空/短/非 4 对齐/零像素全拒）+ `mt_transfer_fill_rect`（dst VA + W/H + color，`w*h != pixels` 大声拒绝——错 factorization 在构建期失败，不上硬件）。C 门禁 16 项进 `pvr_bridge_core_test`（274 Python + 288 C 全绿）；反向掐掉乘积校验 → FAIL，恢复 PASS。未接桥、无发射、会话未碰（probe ref 1）。

## 设计要点

1. 颜色原样搬（`memcpy` 像素字），不解释通道序——r178 边界的直接落实。
2. 表面偏移 3841 与尾 254 为具名常量；他路径布局不同则解析失败（显式），不静默错填。
3. W/H 由调用方提供（平像素数不可分）；本轮不碰活体接线（`mt_tqx_fill_work_prepare` 的 bo/session 部分），接线是下一轮的事。

## 边界

- 朝向/步长沿用 r178 结论（1280×1024 暂定、行连续）；构造器本身不断言朝向。
- 未验证真实发射；`translate_*` 开关未动；无 GPU 工作。

## 下一步（候选）

- 活体接线：observe handler 内解析池 → 构造 rect → TQX fill 发射（`=2` 窗口，需重载），像素回读验证。
