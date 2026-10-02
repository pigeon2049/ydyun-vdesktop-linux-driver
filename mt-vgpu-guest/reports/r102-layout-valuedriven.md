# r102：Layout 门是值驱动（栈位全扫无效，下步枚举映射）

r101 的下步执行完毕，结论转向：harness 栈参数 a6–a15 逐个放非零
buffer（10 次运行，全 exit 0），`R2DCreateSurfaceLayout` 全部返回 3——
`[rsp+0x90]` 门不在 harness 可达范围（16 参数只铺到 caller-stack
`+0x50`），且 rdi 非零已满足。故 `"Null pointer"` 不是缺指针实参，
而是**参数值错误导致的下游空**（格式/枚举查表未命中返回 NULL，
错标为 caller 问题——厂商式甩锅，见多了）。

## 下一步（单点）

读 `R2DCreateSurfaceLayout` 内两个 validator（`call 0x16010` /
`call 0x15ce0`，入参即我们的 u1/u1/u1 + 64/64）+ `CreateTestSurfaceLayout`
本体的查表逻辑，把"宽/高/格式/stride"枚举值从常量反推出来，
再按合法枚举重调。仍离线。
