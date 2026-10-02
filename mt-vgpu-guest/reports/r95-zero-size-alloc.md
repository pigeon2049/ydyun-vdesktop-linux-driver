# r95：TransferContext 卡在 devmem 零尺寸分配（General 堆已定位）

r94 的 B 路一试开花结果——不是 sutu，而是 UMD 的 DebugPrintf 自白，
四连（gdb 断 `PVRSRVDebugPrintf` 抓格式 + 行号）：

```text
DevmemValidateParams: Please request a non-zero size value.   (line 728)
DevmemSubAllocate: Failed! … Allocation size: 0x…              (line 1670)
DevmemAllocateAndMap:1 failed                                   (line 226)
R2DCreateContext: RGXCreateTransferContext failed               (line 188)
```

读到 `RGXTDMCreateTransferContextCCB`（L62914 起）对应段：
`*(create+2)`（devmem ctx）门已过（rogue2d 自备，真值）→
`GetMultiCoreInfo` → `local_190 = FindHeapByName("General")` →
`DevmemAllocateAndMap("TDM ctx store buffer", size≈0x178)` →
SubAllocate 在 ValidateParams 判 `size==0`。即：**传到分配器的
尺寸槽是 0**，不是堆缺失（General 堆 resolved）。离线语料+gdb，
零硬件触碰。

## 下一步（单点）

size 槽的写入者：CCB 内 `iVar5`（`0x178` 或 `cores*0x180`）的装配
vs SubAllocate 收到的实际值——gdb 断 `DevmemSubAllocate` 读参
（rdi/rsi）即定论。若 `cores=0` 导致分支走样，根子在
`GetMultiCoreInfo` 的 fabricated 零回包（bridge `0x1:0xc` 在 shim
无 canned，零填充）——修 shim 一行或可续命；若另有所指，再跟。
