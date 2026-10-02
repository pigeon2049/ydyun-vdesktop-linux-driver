# r96：TDM 控制内存 size-0 根因——计数槽空（rogue2d 栈构造）

r95 的下步执行完毕，根因落地（gdb DebugPrintf 全参 + 语料对读）：
`RGXTDMCreateTransferContextCCB` 内 `FUN_00162560` 要分配
"TDM control memory"，尺寸 = `[P+0x54] << 7`，而 P（=`[create+8]`）
有效、计数槽为 0 → `DevmemValidateParams` 拒收 →
`SubAllocate → AllocateAndMap:1 → CreateTransferContext failed`。
General 堆本身 resolved（r95 已确认），问题不在堆，在计数。
离线，零硬件触碰。

## 证据链（四连格式 + 行号）

```text
DevmemValidateParams: non-zero size (s1=名, line 728)
DevmemSubAllocate: Failed (line 1670)
DevmemAllocateAndMap:1 failed (line 226)
R2DCreateContext: RGXCreateTransferContext failed (line 188)
```

- 尺寸公式：`FUN_00162560`（L39057）`size = [P+0x54] << 7`，
  P = `[CCB.create+8]`（uint*+2 = byte 8）。
- create 结构是 rogue2d 栈上现搭的（`R2DCreateContext@0xafb0` 内
  `lea -0x50(%rbp)` 等），字段来自其 OUT 记录 + rax 链——
  harness 的 u0 参数与此无关（CreateContext 头部只用 rdi 作 OUT）。
- 故：计数 0 是 rogue2d 内部装配问题，不是"传参不对"；
  盲调 harness 参数对此无效（已证：头部忽略 rsi 起）。

## 下一步（单点）

跟 `R2DCreateContext` 内 create-struct byte+8（P）的装配来源
（`mov 0x8(%rax),%rax` 这类链），看计数槽正常应由谁写入
（surface？dev-select？sutu 初始化？）。仍离线。
r92 遗留的 gdb 断点悬空法（绝对地址 + 条件即 hang）记为教训：
无符号内部函数断点必须走 `catch load` + 文件偏移换算，
或直接用 DebugPrintf 格式断点（本轮成功模板）。
