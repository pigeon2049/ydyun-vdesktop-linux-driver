# r87：Rogue2D fabricated spike——95 路桥调用后止于 TDM 共享内存

r86 的 spike 设计一次即跑出大鱼：单个 `R2DCreateContext` 在 fabricated
下走了 **95 条桥调用**（connect→15 堆→PMR×4→reserve/map→sync→event→
unmap/unreserve→15 堆销→ctx 销→disconnect，全进全出，干净 unwind，
返回 3）。卡点精确：`0x89:0x5 BridgeRGXTDMGetSharedMemory`
（IN 0 / OUT 20：eError + 2 个共享内存指针）——fabricated 零填充致
指针全空，后续即收尾。我方桥 `0x89` 组**一个都没实现**
（dispatch 直接 `-ENOTTY`）。全程离线，零硬件触碰。

## 0x89 TDM 组全表（语料 debug 串 + wrapper 行号即证据）

| func | 名 | IN/OUT |
|---|---|---|
| 0 | CreateTransferContext | 0x28 |
| 1 | DestroyTransferContext | 8/4 |
| 2 | SetTransferContextPriority | 0xc/4 |
| 3 | NotifyWriteOffsetUpdate | 0xc/4 |
| 4 | SubmitTransfer2 | 0x6c |
| 5 | **GetSharedMemory** | 0/20（eError + 2 ptr）← 卡点 |
| 6 | ReleaseSharedMemory | 8/4 |
| 7 | SetTransferContextProperty | 0x14 |
| 8 | CreateTransferContext2 | 0xc/0xc |
| 9 | DestroyTransferContext2 | 8/4 |
| 10 | SubmitTransfer3 | 0x6c/4 |

另有 `SubmitTransferDDK2`（新 DDK 同构，r74–r78 的 DDK 主题在 2D 侧重演）。

## 最小实现评估（给立项）

- `0x89:0x5` 的语义 = 给 UMD 一块传输描述符共享内存（server 分配、
  UMD mmap 读写、SubmitTransfer 时引用）。我方实现Anchor：复用
  `pvr_pmr_new`（4K/12 阶已有）+ 现有 mmap 路径——**纯桥侧新增**，
  不碰固件通道、不碰特性开关，是比 DDK2 更小的一扇门。
- 风险：SubmitTransfer（0x89:4/10）的 DM 语义仍未知（T3 家族），
  但 2D blit 的传输描述符比 TA 状态机简单一个量级。
- 顺序建议：先 `0x89:0x5/0x6`（共享内存建销，fabricated 可验 UMD 继续走），
  再跟 SubmitTransfer 的包（抓包 → 对 DM2 信封）。

## 边界

本轮未做活体（fabricated 结论自足）；`0x89` 在我方桥是 `-ENOTTY`，
活体 passthrough 跑同样序列只会更快失败（干净返回 3，无 crash 风险，
但也无信息增量——故未跑）。证据：`r87-rogue2d-spike.jsonl`（95 条）。
