# r345：缺失的生产者是 app——copy 流程从未描述 surface（离线，零硬件触碰）

- **结论**：app 二进制 copy 提交序列（`0x3c00–0x4030`）的 PLT 调用逐项清点：`RunStats`×2、`fail_if_error`×3、`CallocUserModeMem`×2、`calloc`×2、`SubAlloc`、`Acquire/ReleaseCPUMapping`、`SemWait`、`QueueTransferNew`——**没有任何 surface 创建/设属性/绑定调用**（动态导入表亦只有 `Create/DestroyTransferContext + QueueTransferNew` 三个 `RGXTDM*`）。copy 流程是：建 transfer 上下文 → 清零描述结构 → 直接排队；源/目 surface 从未被描述，描述符表恒空，计数恒 0，`release` 内 `0>=0` 断言是其直接后果。与桥无关（101 调用全 0 依旧），与 `=2`/默认无关（门控只决定走哪条 submit 路径，表空则新路径必死、旧路径在桥侧拒收）。
- **`CreateTransferContextCCB`（`0x8b3a0`，`0x1220` 字节）旁证**：调 `PVRSRVCallocUserModeMem`×3（calloc 语义——零填充的出生证）、`SubmissionDestroy`、`GetFeatures`、`DevmemGetHeapBaseDevVAddr`；`CreateTransferContext` 本体（`0x8c5c0`）只是参数改写后 `jmp CCB`。ctx 空壳出厂即零，之后 copy 路径无回填者。
- **r333–r345 收官**：骨架搭完→容量断言死→表内无人写→零值胎里带来→空壳出自序言→ctx 随 job 来→调用方尾跳→app 未描述 surface。copy 中止的完整因果链闭合，缺口在 vendor 测试程序自身的 setup（或它依赖的、我们未实现的某默认 surface 语义——后者需 vendor 参照才能判定，如实记为开放项）。

## 实测与边界

1. 本轮零硬件触碰：未加载、未重载、未跑 UMD；`bridge ref 0` / `probe ref 1` 不变，会话 freeze 继续。
2. 证据：`objdump -d`（app `0x3c00–0x4030` 调用清点 + `objdump -T` 导入表 + lib `0x8b3a0/0x8c5c0` 入口形态）；r319（transfer 上下文建成）与 r342（job/ctx 活体值）交叉。
3. 开放项：vendor 完整 copy 流程是否在 `Create→Queue` 之间调了此处没有的 surface 接口（如 `RGXTDMSurfaceCreate` 系）——需 vendor 参照或 fill 路径类比，不能仅凭缺席定罪；但“本 UMD copy 流程无 surface 描述”是实测事实。

## 下一步

1. copy 线在此关账（RE 已到 app 边界，再往下是 vendor 行为复刻，另立大项）。建议重心回 transfer-fill 扩展（copy 语义经 `SubmitTransfer3` 的 translator 实现，而非修 UMD）或 TA/CDM；由用户拍板。
