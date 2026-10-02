# r75：DDK2 结构体全映射 + rsi 对象需求定位（崩溃精确归因）

r74 遗留的 DDK2 入参 shaping 有实质进展：`RGXKickSyncDDK2` 的第 3 参数
结构体已静态全映射（含 update/check 双数组 + 统一 server 数组布局）；
`+1490` 崩溃精确归因到 **rsi 对象（第 2 参数）`+0x28` 为 NULL**，
不是 r74 猜的 `[struct+0xd0]`（自我修正，见下）。rsi 必须是一个比
CCB 创建对象（`0x30` 字节）更丰富的对象，首位候选是 render 上下文
客户端对象（`0x330` 字节）。全程离线 + fabricated，零硬件触碰。

## DDK2 客户端结构体（第 3 参数）映射

| 偏移 | 形状 | 含义 |
|---|---|---|
| `0x0` | u32 | update 条数（≤12，`cmpl 1…0xc`） |
| `0x8+i*0x10` / `0x10+i*0x10` | u64/u64 | update 条目 i（注意值域 64 位，与 check 侧 u32 不同） |
| `0xc8` / `0xd0` | u64 ptr | 原样搬进目标结构 `+0x8`/`+0x18`（只转运、不解引用） |
| `0xd8` | u32 | check 条数（≤12） |
| `0xe0+i*0x10` / `0xe8+i*0x10` | u64/u64 | check 条目 i（句柄 + 64 位值） |
| `0x1a0` / `0x1a8` | u64 | → server 数组 slot 12 的 `+0x188`/`+0x190` |

server 侧统一数组（`Calloc(u32@0x0 + u32@0xd8)`）：slot 0–11 check
（步长 `0x20`：flag + u64 + u64），slot 12 分隔（含 `0x1a0/0x1a8` 内容），
slot 13 起 update（同源模板 `{2, ptr, ptr}` + 逐条目覆盖）。

## 崩溃归因（gdb 6/6 同址，自我修正 r74）

```text
DDK2+1490: mov %rax,0x48(%rdx)   # rdx=0
```

r74 误读为 `rdx=[struct+0xd0]`；重读 `5353c–53592`：`0xc8/0xd0` 只是被
转运，`rdx` 在 `53556` 被 `mov 0x28(%rbx),%rdx` 覆盖——
**崩溃源是 rsi 对象 `+0x28`**。`+0x20` 是每 kick 自增计数器
（`mov/add/mov` 三件套），`+0x18` 被转发放到签发调用参数。

CCB 创建对象只有 `0x30` 字节且 Create 只写 `0x0/0x10/0x14–0x16`
（`+0x8/+0x18/+0x28` 恒零）——所以 rung8 形状必崩。rsi 另有其物。

## rsi 候选：render 上下文客户端对象

`RGXCreateRenderContextCCB@0x7b6c0`：`Calloc(0x330)`，从输入结构读
`+0x30/+0x34` 等字段。0x330 字节的富对象，能容下 `+0x18/+0x20/+0x28`
全套——是 DDK2 rsi 的首位候选。验证（把 render 对象喂给 DDK2 rsi）
留给下一轮；另 `RGXCreateKickSyncContext@0x52550` 已证伪（就是 CCB
包装的直接调用，无更丰富的对象）。

## 附带

- 试探 `0xc8/0xd0` 指向有效堆缓冲（`u64 26 200/208 b28`）仍崩于同址，
  与归因一致（崩溃与这两槽无关）——保留为"已排除"记录。
- 命令行笔误教训一则：一次把 `u32 6 52` 写成 `u32 6 48`（render scratch
  结构走样），4 连崩后才发现——长 harness 命令必须逐项 diff 已知-good
  前缀，已用固定前缀脚本避免复发。
