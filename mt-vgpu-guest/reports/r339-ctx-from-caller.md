# r339：ctx 来自 job+0x10，序言零写入——建表责任在调用方（离线，零硬件触碰）

- **结论**（objdump 对齐反汇编，`nm -D` 锚定）：`TQJobSubmit`（`0x5ff00–0x60970`）调用序为 `QueueValidate(0x5fffb)→QueryTimer×2→DestroyPrepare+0x880/+0x860→BlitInit(0x60125)→CheckFences→LookUpEOT→…→release+0x3320(0x601dd)`；ctx（`0x…0240`）是 `0x600f5: mov 0x10(%rbx),%rsi` 从 job 对象一次性取出、直传 `BlitInit` 的——**调用前已存在**。序言区（`0x5ff00–0x60125`）**零处写 `+0x58`**、无 malloc/间接调用（5 个直接调用全点名），链既非此处建、亦非此处填。建表责任在 `TQJobSubmit` 调用方（sutu 应用层或函数指针分发——lib 内无直接 `call 0x5ff00`，如实记，未硬指）。
- **r333–r339 链条终版**：job/ctx 空壳由调用方带来 → 序言只读不写 → BlitInit→release 全程零填充 → release 容量断言 `0>=0` 自杀。与桥无关（101 调用全 0 依旧）。copy 中止的“缺失生产者”定位到 **TQJobSubmit 调用方一侧**。

## 实测（离线：objdump/nm，无执行无加载）

1. `0x5ff00–0x5ffb` 序言（大栈清零 + `job+0x18` 读取）+ `0x60022–0x60080`（QueryTimer 后取值存栈）+ `0x600e6–0x60125`（`job+0x10→rsi` 交接）三段判读；`0x600e1–0x600e5` 系数据误 decode，跳过。
2. `+0x58` 写扫描零命中；`call *` 间接调用零条；`TQJobMultiSubmit`（`0x60970`）为另一入口，未展开。

## 边界与下一步

1. 下一刀（活体一发）：`TQJobSubmit` 入口 `bt`——r333/r334 的 `bt` 静默发生在 abort 点，若入口 `bt` 可用则调用方直接点名；同时倾印 `job+0x10` 上游指针 Serr。窗口脚本复用 r338 模式。另行开轮。
2. 本轮零硬件触碰：未加载、未重载、未跑 UMD；refs 1/0 不变，会话 freeze 继续。
