# r91：import 拒绝点收敛——桥后 UMD 侧校验（eError 居尾无辜）

r90 的新卡点已收敛到一小片：`0x6:0x6` 桥本身（ret=0 入金全对）之后、
`0x6:0x4` 之前，UMD 在 `FUN_001979b0 / FUN_00197630 /
AcquireCPUMapping` 一带判负，直接 `Unref + Release×2` 走人。
同时排除首嫌疑：厂商头证实 `OUT_PMRLOCALIMPORTPMR =
{align, size, hPMR, eError}`（eError 居尾 24 偏移），我方 header
与 shim canned 一致——"错误码错位"不成立。离线语料，零硬件触碰。

## 链（import 包装 `FUN_00132200@L7233` 内外）

```text
MTSRVDevmemLocalImport → FUN_00197100 → FUN_00197500（分配 0x1a8 上下文）
  → FUN_00197870（预检，通过：桥调用已发出为证）
  → FUN_00132200：0x6:0x6（IN 8 句柄 / OUT 28）→ *param_3/4/5 回填
  → FUN_001979b0 + FUN_00197630（OUT 值后处理）→ return 0
TQPMR_MapMem 随后应调 Unmake（0x6:0x4）——trace 里没有，
故判负点在后处理三者之一（OUT 值相关）或 AcquireCPUMapping。
```

## 已排除 vs 待查（判别式已备好）

- 排除：eError 位置（厂商头 `common_mm_bridge.h:235` 实锤居尾）；
  桥 ret（shim 恒 0）；预检（桥已发出）。
- 待查：把 r2d2 的 `0x6:0x6 out_written` 与 rung8 通过版逐字节比对——
  若一致，则 bug 在**输入相关**校验（`0xa000` 假句柄流入的
  `GetImportUID` 或对齐检查），而非 OUT 内容；反之则是 OUT 字段。
  一条 diff 即定论，下一轮执行。

## 附带（TQPMR 副产品）

- `MapMem` 拷贝 `[0x18 + count*0x4c]` 进用户态（描述符表首 u32 count@+4，
  步长 `0x4c`/frag `0x44`）——SubmitTransfer 的描述符输入格式已见一半。
- `MapUSCMem` 多一步 `MTSRVMapToDevice`（`0x6:0x13` 系，我方已实现）。
