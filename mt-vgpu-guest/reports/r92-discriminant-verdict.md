# r92：判别式 verdict——OUT 一致，输入侧空连接是嫌疑

r91 的判别式执行完毕，结论干净：r2d2 与 rung8 的 `0x6:0x6 OUT`
逐字节相同（4 条 vs 5 条，全同 canned）——**拒绝与 OUT 内容无关**，
bug 在输入侧。顺藤摸到 `TQPMR_MapMem → AcquireCPUMapping(*(lVar2+0x10))`：
该槽位在 `Calloc(0x40)` 后无任何写入（桥 OUT 进的是 `+0x30/+0x38`），
空连接进映射即返 3。离线语料 + trace，零硬件触碰。

## 证据链

1. OUT 比对：`001000…00000000` ×9 全同 → eError/align/size/handle
   全部排除（r91 的 eError 排除再确认一次）。
2. 路径：import(76) → 无 Unmake 桥（本地函数，无痕正常）→
   无 mmap → 直接 unref(77)。`AcquireCPUMapping` 内零 syscall 即判负，
   故判据全在已持有的数据上。
3. 空槽：`lVar2 = Calloc(0x40)`；`FUN_0018aaa0` 不写 `+0x10`；
   桥回填 `+0x30/+0x38`；`+0x10` 保持 0 → `param_1==0` →
   `"psConnection invalid"` → 返 3 → `Unref + Release×2` 走人。
   与 trace 的"无 mmap、无桥调用即放弃"完全吻合。

## 待证伪（下一条命令即定论）

`+0x10` 是否真无人写：gdb 断 `TQPMR_MapMem` 入口读
`[lVar2+0x10]`（零即实锤）；或读完 `FUN_0018aaa0` 全文找隐藏写入。
若实锤，则 Rogue2D 需要的不是"更好的句柄"，而是前置的连接对象
装配（`8aaa0` 的真实职责）——整形目标从"调参"变为"补调一个创建调用"。
