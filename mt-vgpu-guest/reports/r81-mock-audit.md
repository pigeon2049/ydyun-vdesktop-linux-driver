# r81：mock 路径排查 + DMA 活体验收（用户批准的真机测试）

结论：桥内三类并存——**真干活**（PMR/arena/DMA/台账/inspect）、
**成功空桩**（10 个 `pvr_stub_ok` + OOM/ZS/compute/render 句柄作坊）、
**静态真形**（connect/堆表/info 页：形状对、内容非 GPU 实采）。
最具行为影响的 mock 是 `EventObjectWait` 直接成功——UMD 的等待逻辑
永不阻塞。本轮活体 `pvr_dma_smoke` PASS（refs 38→39→38），DMA 路径
确为真映射。零残留，會話未動。

## 活体验收（本轮，renderD128，无 rmmod/timeout/GPU 工作）

```text
map returned mapping=0x1000, session refs 38 → 39
PMR released, session refs now 38 (expected 38)
PASS: four-page PMR DMA-mapped and unmapped; no GPU work submitted
```

dmesg（本轮新行 + 历史行）：`VM plan … va=0x5000000000 bytes=16384 pages=4`
（= smoke 的 TEST_VA，plan 真记账）、`arena close high_water=4/512`
（按文件释放）；映射细节见 once 行
`DMA domains: dma_iova=0x248f9b000 gpu_pa=0x8a48f9b000 pages=4`
（`pr_info_once`，同会话同机制）。会后 `pending=0 completed=23`、
`objects=34`，引用回落 38，无新增 WARN。
证据：`reports/r81-live-dma-proof.txt`。

## 审计表（`pvr_bridge_dispatch` 全分支，行号即证据）

**A. 真干活（有内核/硬件语义）：**
| 入口 | 行为 |
|---|---|
| PMR alloc/import/unref/make/unmake（0x6:0x9/0x6/0x7/0x3/0x4） | arena 真内存 + 引用计数（本轮活体验证分配/释放） |
| Map/Unmap/Reserve/Unreserve（0x6:0x13/0x14/0x15/0x16） | plan 台账 + cover 绑定；DMA 注册走真 `dma_map`（本轮验证） |
| ctx/heap 建销（0x6:0xf/0x10/0x11/0x12） | 对象台账（r52 阶梯验证） |
| sync block（0x2:0x0） | 真 4 KiB PMR；`vaddr`=host 指针（已注释的近似，非 GPU VA） |
| kick submit（0x88:0x2/0x3/0x4） | 真读用户数组 + 句柄解析 + 记账（r63/r73 活体） |
| compute/render 建销、ZS 建销 | 句柄生命周期台账（见 C 类限定） |

**B. 成功空桩（返回 0，无任何效果）：**
`pvr_stub_ok` ×10：Disconnect、ReleaseInfoPage、Release/GlobalEventObject、
**EventObjectWait**、EventObjectClose、AlignmentCheck、GetMultiCoreInfo、
EventObjectWaitTimeout、FreeSyncPrimitiveBlock、SyncAllocEvent
（`mt_pvr_bridge.c:2245`：零填充 OUT 即返）。其中 **EventObjectWait
直接成功是最危险的 mock**：UMD 任何等待同步的逻辑都被短路，
一旦将来有真 fence 语义，首当其冲。
`pvr_cmd_oom_stats`（1757：忽略输入、零 OUT）、`pvr_cmd_hwperf`
（2199：真 4K PMR 但只当句柄用，无 perf 语义）亦属此类。

**C. 句柄作坊（台账真，语义空——输入被丢弃）：**
`pvr_cmd_zs_create`（1540：13 参数**一个不用**，直接发号）——这就是
"ZSBuffer 未驱动"的桥侧一半原因（另一半是 UMD 侧缺 heap 指针，§5.3）；
compute/render create（"Object lifecycle only"，1580 注释自认）。
UMD 拿到的句柄在后续调用中仅做存在性检查，**一旦翻译器让这些句柄
承载真实语义（如 ZS 附着到绘制），必须重写**。

**D. 静态真形（形状来自厂商规范，内容非实采——刻意选择）：**
connect（caps 归零，`mt_pvr_device.h:151`）、features+0x54<2（143–145，
legacy 钉死，r78）、堆表 15 项（蓝图抄写）、info 页（三字常量）。
这类不是"没做完"，是 bring-up 期 validated 的地基；动任何一项
（如特性开关）都要按新 DDK 路径重验。

**E. 明确拒绝（S4 边界，非 mock）：** `-ENOTTY`（0x81:0x5 等真提交入口、
未知 function）。诚实失败，好于空桩成功。

## 给后续的含义

1. 翻译器能依赖的"真"：PMR 内存 + DMA IOVA、plan 台账、sync PMR 内容、
   kick 数组读取——S4-3 handoff 的地基是实的。
2. 翻译器**不能**依赖的：EventObjectWait 语义（恒成功）、ZS/compute/render
   句柄含义（空号）、OOM/hwperf（零）、GPU VA（host 指针近似）。
   首帧翻译设计必须把"等待"语义自己实现（fence 轮询在我方 live 侧已有先例），
   不能指望 UMD 的 wait 走通。
3. mock 清单至此冻结为基线：以后任何"UMD 行为异常"，先查本表再怀疑 UMD。
