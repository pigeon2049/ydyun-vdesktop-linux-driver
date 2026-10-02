# r101：Layout 可执行；新门是 TestSurfaceLayout 空指针 + harness 关键字陷阱

自主推进两则硬货（全离线 fabricated）：

## 1. harness 关键字陷阱（真 bug 级教训，已中招）

`call` 的参数分隔符含字面 `u64/u32/buf/connect/...`——把裸 `u64`
当整数参数传（如 `call … b30 u64 u64 …`）会**提前截断参数表**，
余下部分被当成 poke op 执行，`bufs[0]=NULL` 直接段错误
（`umd_connect_harness.c:159-163`，`memcpy(NULL+off)`）。
r72 至今都用 `u0/u1/u0x33` 形式从未踩过；要传 64 必须写 `u0x40`。
以后所有 harness 命令行禁用裸类型名字面量。

## 2. Layout 新门：`CreateTestSurfaceLayout: caller passed in a Null pointer`

修正命令行后（`u0x40` 宽/高）三调用全 clean 返回（exit 0，
Layout → 3），DebugPrintf 新增一行（line 492）：
`CreateTestSurfaceLayout` 拒收空指针。候选：
(a) 某 u0/u1 参数位实为结构体指针；
(b) `b31`（64B 全零）是 surface 描述块，其字段（宽/高/格式槽）
全零导致下游判空——b31 内容整形是下步（读 Layout 对 b31/a6 的
字段读取 offsets，即上轮 [rsp+0x90] 链的延续）。
