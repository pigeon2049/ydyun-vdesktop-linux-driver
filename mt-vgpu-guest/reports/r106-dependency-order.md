# r106：依赖倒挂澄清——Surface 要 Context 的堆，Context 先天不足

本轮关键澄清（省掉未来 N 轮弯路）：`R2DCreateSurface` 系走得更深了
（492/512 双门消失，142 条调用不变），但新边疆
`hHeap invalid → MIW failed → AllocateSurfaceLayout failed`
不是 surface 整形问题——MIW 要的堆句柄来自 `R2DCreateContext` 的
OUT 记录，而我们的 Context 因 TransferContext 失败（返 3）根本没填堆。
**依赖倒挂：先修 Context（+0x54 计数），Surface 自然跟进；**
继续调 surface 参数是缘木求鱼。

## 链条（当前精确态）

```text
R2DCreateContext
 ├─ StaticMem：通（r93 import 修后；MapMem/MapUSCMem 桥全发）
 ├─ TransferContextCCB：0x89:0x0 桥通 → devmem 分配
 │    └─ [P+0x54] = 0 → size-0 → return 3   ← 全链唯一真卡点（r95–r99）
 └─ OUT 记录堆句柄：空（因上失败）
R2DCreateSurface（Layout 内调）：门全过 → MIW 要堆 → hHeap=0 → 返 3
```

## 下一步（唯一）

`[P+0x54]`（P = create+8，R2DCreateContext 栈结构体内迁量）的写入者：
跟 `R2DCreateContext`（rogue2d `0xafb0` 起）内 create-struct 的装配，
看 byte+8 取自哪（`0x78(%r12)` 链的源头）。仍离线。
