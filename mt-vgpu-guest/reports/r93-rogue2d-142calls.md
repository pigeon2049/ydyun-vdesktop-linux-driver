# r93：Rogue2D 95→142——两次"零句柄"修复，打到 TransferContext 创建

r92 的证伪链条开花结果：`0x6:0x3` 零 import 句柄确为 Unmake 拒绝根因
（shim 补非零后 +41 条）；同一模式再解 `0x89:0x0`（零 context 句柄 →
补非零 → +1 条，`DestroyTransferContext` 出现）。Rogue2D spike 现深
142 条桥调用，止于 create 之后约 16 条处（新边疆待下轮）。
全程离线 fabricated，零硬件触碰。

## 三次推进（行为即证据）

| shim 补丁 | 调用数 | 新到达点 |
|---|---|---|
| 0x89:0x5 非零（r90） | 95→100 | MapMem（0x6:0x3/0x6:0x6） |
| 0x6:0x3 非零 import（本轮） | 100→141 | CreateTransferContext（0x89:0x0） |
| 0x89:0x0 非零 context（本轮） | 141→142 | Destroy（0x89:0x1，create 后深入 16 条） |

## 模式沉淀（"零句柄"三连杀）
UMD 侧表查询（Unmake/first-use）对零句柄一律拒绝，且**静默**
（无桥调用、无 DebugPrintf 给出句柄值，只报上层失败）。
以后任何 fabricated 卡点，第一反应：查该点消费的句柄是否零填充。
eError/尺寸/ret 全对也枉然——句柄是另一维度的门。

自我修正（r92）：r92 的"AcquireCPUMapping 空连接"嫌疑被证伪——
gdb 实测其 rdi 为有效堆指针且只命中一次；真正的拒绝点是
Unmake 查表（零 import 句柄）。旧报告留档不改，特此更正。

## 下一步

create 后 16 条窗口（idx 82–98）：列出新增桥调用，定位下一个零值/
缺件。仍离线。证据：`r93-rogue2d-142calls.jsonl`。
代码提交 `7ad8663`（27 行，-Werror 干净）。
