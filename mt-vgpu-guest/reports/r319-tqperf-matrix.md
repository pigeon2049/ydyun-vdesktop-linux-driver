# r319：tq-perf 三连发矩阵（批准执行）——abort 与配置无关

- **结论**：同一窗口（`=2` observe）三连发 `sysmem(-src -dst)` / `small(640×480)` / `control`：**全部即时 134、全部 8200 行、全部 101 调用全零、末调用全是 `0x6:0x13` map**。abort 与表面类型/尺寸无关——不是 PMR/尺寸问题，是 TQJobSubmit setup 的结构性前置缺失。core ×3（未单存，r317 的 core 同形已入库）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**无代码改动。
- **反汇编进展（离线续）**：abort 桩定位（`ud2;ud2;call abort`，file `0x2c8c0` 一带，`mov $0x48575032` 标签）；TQJobSubmit 内调用链（`RGXQueueValidate → RGXQueryTimer×2 → TQ_BlitInit → TQ_CheckFences → TQ_LookUpEOT → … → RGXReleaseCPUMappingZSBuffer+0x3320` 后 abort）。0x60180 一带线性反汇编错位（数据表混杂），abort 分支条件未命名——留待 GDB 活体断 abort 桩读参（r296/r314 同手法）。

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → `=2` → 三连发（各 45s 上限，实际即时 abort）→ 读 trace → 拆桥 → 默认 → L3 双绿 → 拉桌面（用户中途重开挡回一次 rmmod，二次停后关账）。
2. 证据：`r319-{sysmem,small,control}.jsonl`（0600，各 8201 行... 实测 8200）+ 三 stdout（0600）；暂存区已清空。门禁沿用（386+299，无代码改动）。

## 边界与下一步

1. r320（离线或小窗口）：GDB 断 abort 桩（file `0x2c8cc`，按 ASLR 基址换算）读 abort 前寄存器/栈——命名 abort 条件；或追 `RGXReleaseCPUMappingZSBuffer+0x3320` 返回值（bridge 侧 `0x6:0x14/0x16` 全 0，疑点在 UMD 侧对返回的解读）。
