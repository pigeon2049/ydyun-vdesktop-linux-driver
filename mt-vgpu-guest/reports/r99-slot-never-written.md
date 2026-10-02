# r99：+0x54 从未被写入——缺的是前置调用，不是整形

r98 的修正案：三种堆布局下一致结论——`P+0x54` 在
`R2DCreateContext` 全流程中**从未被写入**（r97 读 0；
r98 的 calloc 复用是布局相关的分配器噪声，非稳定 writer；
本轮 CCB 入口后 watch 全程静默）。r98"悬空"表述收回，
准确表述是"未初始化"：我们只调了 `R2DCreateContext` 一个入口，
而填充该槽的 R2D* 前置调用（surface/layout、dev-select 链）
根本没跑。离线 gdb，零硬件触碰。

## 证据

- 固定 trace 下 P 地址确定（0x…9f50 三次一致），CCB 入口后
  硬件观察点零命中 → 入口之后无人写。
- r98 的两次 calloc 命中换布局即消失 → 非稳定写入者。
- `R2DCreateContext` 头部只用 rdi（OUT），我们的 u0 参数无辜；
  缺的是**调用序列长度**，不是参数值。

## 下一步（序列延伸，而非结构整形）

按 rogue2d 正常顺序补前置调用（fabricated，逐个加，看 P 槽何时被填）：
`sutu_DevInit(非零参，0x15890 分流）→ sutu_dev_select →
R2DCreateContext → R2DCreateSurfaceLayout → R2DCreateSurface`，
每加一步查 P 槽。哪一步填上，哪一步就是缺失的前置。
