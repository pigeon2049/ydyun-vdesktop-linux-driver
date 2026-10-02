# r89：TDM 下游验证——Map 链全用已实现桥 + SubmitTransfer2 骨架

r88 的 H1 假设大幅走强：`0x89:0x5` 返回的 PMR 句柄下游，
`TQPMR_MapMem` 只调 `0x6:0x3→0x6:0x6→0x6:0x4→mmap`
（全已实现），`TQPMR_MapUSCMem` 同理 + `MapToDevice`
（`0x6:0x13` 系）。即：**新桥一旦加载，Rogue2D 的共享内存
全链路没有第二个缺口**，直到 `SubmitTransfer2/3`。
`SubmitTransfer2（0x89:0x4）` 包装器骨架已列（IN 0x6c/OUT 4，
20 参数逐槽）。全程离线语料，零硬件触碰。

## Map 链（语料行号）

- `TQPMR_MapMem=FUN_00157630`（L31121）：MakeLocalImport →
  DevmemLocalImport（flag `0x1810`，"PMRMem"）→ Unmake →
  AcquireCPUMapping → 用户态拷贝 `[0x18 + count*0x4c]` 字节
 （描述符表：首 u32 count@+4，步长 `0x4c`，frag 对 `0x44`）。
- `TQPMR_MapUSCMem=FUN_00157870`（L31189）：同三件套
 （flag `0x301`）+ `MTSRVMapToDevice`。
- 结论：H1 的 PMR 句柄在两条 Map 路径都可消费（存在性层面）；
  剩余不确定性只剩 UMD 对"同一 PMR 复用两槽"的容忍度——活体一把验。

## `0x89:0x4 SubmitTransfer2` 骨架（L11390，`&local_90` 为基）

`+0x00`p20 / `+0x08`p2 / `+0x10`p16 / `+0x18`p6 / `+0x20`p7 /
`+0x28`p13 / `+0x30`p11 / `+0x38`p17 / `+0x40`p5 /
`+0x48`CONCAT(p9,p8) / `+0x50`CONCAT(p19,p18) /
`+0x60`CONCAT(p3,p14) / `+0x68`p15(u32)；OUT 4B eError。
回填到 SubmitTransfer 调用点是下一步（静态，未展开——等加载窗口
先让 `0x89:0x5` 跑通，包自然有人发）。

## 加载窗口的验收判据（提前写好，届时照单抓包）

1. fabricated 重放：`0x89:0x5` OUT 非零（ expect 双 handle 同值）→
   UMD 越过卡点（新桥调用出现 `0x6:0x3` 即赢）。
2. passthrough：同上 + `MapPMR/mmap` 真走 → Rogue2D 继续深入
   （`SubmitTransfer` 包是下一证据）。
3. 任一判据失败 → 按 r88 升级路径拆双 PMR（H1 证伪预案已就位）。
