# r97：P 槽位直接观测——有效指针，计数 0，相邻有值

r96 的下步执行完毕（gdb 断 `RGXTDMCreateTransferContextCCB` 出口参数，
exported 符号，无需算地址）：create 结构在调用者栈上，
`[create+8] = P`（堆，有效），且：

```text
[P+0x50] = 0x3000   (12288，疑尺寸)
[P+0x54] = 0x0000   (u32，尺寸公式的计数源 → size = 0<<7 = 0)
[P+0x58] = 0x000f   (15，疑堆数/标志)
```

即：不是空指针，不是谁没写——写了，但**计数值槽是 0，
而相邻槽有值**（0x3000/0xf）。问题从"谁没写"收敛为
"`+0x54` 的语义是什么、正常由哪一步填"。

## 排除项

- P 本身合法（堆对象，非野指针）→ r96 的"装配源"方向对，
  但目标从"找 P"变为"找 +0x54 的写入者"。
- 候选写入者：devmem 上下文初始化、surface/layout 创建、
  sutu 设备选择——都发生在 `R2DCreateContext` 内部、
  TransferContext 创建之前；我们只调了 CreateContext 入口，
  其内部前置步骤可能因某条件跳过了计数填充（而非 crash——
  干净返回 3 说明是"缺省值"路径）。

## 下一步（单点）

对 P（运行时地址当次有效，gdb  deterministic 下可复用）下
硬件观察点（`watch *(P+0x54)`），看 TransferContext 创建之前
谁写过它（若从未被写 → 缺省零 → 找"本应调用但未调用"的前置步骤，
对照点是 `R2DCreateSurface*` 一系）。
