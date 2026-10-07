# r313：图案几何落定 + solid 证实（批准执行）——比对输入只差点名

- **结论**：poolbox 活体读数：`0x101b: first=last=5243393 val=0x4c`（尾部单字节标志）、`0x1032: first=3841 last=5246717 val=0xff0000ff`（**恰为像素体全域**：3841=HEAD，5246717+4=5246721=池尾-254=TAIL；solid 全覆盖实锤）。源池 solid 成立 → fill 颜色（池 `0xff0000ff`）正确，RED/GREEN 排除亦自洽。UMD 仍 FAIL——在 solid+对池+对色之后，只剩：**比对读哪两块内存**（源/目之外的第三者？CPU 参考缓冲？stride？）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → 五开（含 r312 构建）→ 真实 blit（exit=1）→ 读 poolbox/override/fire/bump 行 → 拆桥 → 默认 → L3 双绿 → 拉桌面。
2. dmesg 执行值（上轮）：poolbox 两行如上；`pristine override pool=0x1019`；`fired=1 ... todst=1`；bump 成功。
3. 证据：`r313-poolbox.jsonl`（0600）+ `r313-blit-stdout.txt`（0600）；暂存区已清空。无代码改动（r312 构建直接上机）。

## 边界与下一步

- 下一步（r314，无代码）：GDB 断比对循环（blit 程序内 `0x4624` 处，按 ASLR 基址换算）读 `rcx/rsi/r13d`——**比对双方地址 + 字节数一次点名**，终结“读哪”问题。这是纯观察窗口（`=2` 即可，translator 都不用）。
