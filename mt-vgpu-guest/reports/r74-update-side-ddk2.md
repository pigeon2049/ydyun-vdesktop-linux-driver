# r74：update 侧不在 RGXKickSync 路径上（证伪 + DDK2 候选定位）

本轮目标是 update 侧数组偏移（r72/r73 遗留）。结论是证伪式的：
`RGXKickSync` 的客户端结构体里**只有一个条数（`0xd8`）**，update 侧没有
对应的数组输入——`0x1b0`  poke 与第 4 参数结构体都不能让 `update`
变非零（各 4/4 fabricated 验证）。同构的 update 形态只出现在另一个
入口 `RGXKickSyncDDK2` 里，但它需要更多有效入参（rung8 形状的参数
在其 `+1490` 处稳定空写崩溃）。全程离线 + fabricated，零硬件触碰。

## 实测

1. `0x1b0=1`（`buf 26 1024`，其余全零）：`RGXKickSync → 0`，
   桥包 `check=0 update=0`（4/4）。`0x1b0` 不是 update 条数，
   至少不是本路径的。
2. 第 4 参数给 64B 结构体（`u32@0=1`，a3 非空路径）：`→ 0`，
   桥包仍 `check=0 update=0`（4/4；证据 `r74-update-side-negative.jsonl`）。
   a3[0] 只决定一个 flag（`a1+0x10` 是否进签发调用），与 update 数组无关。
3. 静态穷举：`RGXKickSync` 内对 b26 的读只有 `0x0/0x8/0xd8/0xe0…0x1a8/0x1b0`
  （`cmpl` 只比 `0xd8`），不存在第二个条数——update 数组输入在本函数
   无来源。

## DDK2（`RGXKickSyncDDK2@0x52fc0`，新发现的同构入口）

- 头部 `(rdi=conn, rsi=?, rdx=struct)` 三非空检查；其循环与 check 循环同构
  但字段更宽：`count@0xd8`，源 `{u64@0xe0+i*16, u64@0xe8+i*16}`
  （注意第二个也是 8 字节 load），目标步长 `0x20`（u64+u64+flag），
  server 数组大小 = `u32@0x0 + count`，同样 12 条展开。
- rung8 形状参数（`b7* b12* b26`）在其 `+1490` 处 6/6 确定性崩溃：
  `mov %rax,0x48(%rdx)`，`rdx=NULL`（前一条是客户端对象 `+0x20` 计数器自增）。
  DDK2 要的入参比 rung8 给的多，形态待下一轮解。
- 崩溃前未发出任何桥调用（trace 无 `0x88`），故 DDK2 的桥映射仍未知；
  不排除它才是 update 侧（乃至新版统一 kick）的入口。

## 输入规约现状（给翻译器）

- check 侧： closed（r72 映射 + r73 活体 `1/1`）。
- update 侧：`RGXKickSync` 路径**无输入**（本轮证伪）；候选 `DDK2`，
  下一步是解它的完整入参（从 `+1490` 的 `rdx` 来源倒推），不是继续
  在 b26/a3 上扫偏移——方向修正，避免无效穷举。
- 附带：`buf 26` 取 1024 时 render-create 段错误连击（10/10），512 则稳；
  仍是 UMD 堆布局敏感的老 flake（r72 已记），离线范畴。
