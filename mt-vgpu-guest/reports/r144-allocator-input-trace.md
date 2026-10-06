# r144：分配器输入追踪——崩溃是 harness 传参差一层间接，不是桥缺口（主线诊断）

- **结论**：`SubmissionBufAlloctorCreate` 崩溃的直接原因是 harness 把
  `&b5`（16 字节 pair 地址）当 `hDevMemContext` 传给 `RGXCreateKickSyncContextCCB`，
  而 DDK2 要的是 `b5[0]`（device-mem 上下文结构体指针，即 `b5*`）。
  改传 `b5*` 后同链**全部返回 0、harness exit 0、零崩溃**；
  全链唯一非零仅剩 `0x2:0x8`（IN 4/OUT 4，非致命）。
  Legacy 下 `&b5` 能跑是巧合（只读 `b5[1]`，而 devmemctx 创建把两个槽写成同一指针）。
- 本轮零驱动改动（只读追踪 + 用户态传参实验）；probe 未动。
  机器重启，/tmp 易失证据（trace/gdb 脚本/探针）已清空，会话需重建；
  本报告以轮内实测输出为据（见下），r145 活体时复验。

## 实测（执行过，输出在轮内记录）

1. 堆表无辜：gdb 断 `MTSRVFindHeapByName`，同链 8 次查询（PDS×2、General×3、
   USC×2、ComponentControl×1）**全部命中同一 ctx 并返回**——桥 15 堆表服务正常，
   DDK2 要找的 "General" 存在且可达。崩溃是第 9 次查询，换了对象。
2. 崩溃点：DevmemFindHeapByName 线性搜 heap 指针数组（`mov (%r15),%r14`，
   r15=0），找 "General"（rsi 实测）；数组内有 0x8000000000（General 基址）
   条目但名指针为空（或步长错位读到空槽）——搜的不是上面那个完好表。
3. 入口实测：断 `SubmissionBufAlloctorCreate` 入口，render 路径传 devmemctx
   结构体（成功返回），CCB 路径传 `&b5`（随后崩溃）。`b5={0x30ptr,0x30ptr}`，
   分配器取 `param_1[1]` 即 `b5[1]` 当堆表基查 "General"，读到错位内存。
4. 判定实验（同会话、`=2` 桥、仅改 CCB 一个传参 `b5`→`b5*`）：
   connect/devmemctx/render/syncprim/CCB/destroy **全部 0**，exit 0；
   轨迹 98 调用，仅 `0x2:0x8` 非零（`-ENOTTY`，destroy 照常回 0）。
   DDK2 CCB 全生命周期序列：`0x82:0x12` → `0x2:0x7`+`0x2:0x2` →
   `0x88:0x5` → `0x2:0x7`+`0x2:0x2` → `0x88:0x6` → `0x2:0x8` → `0x2:0x2`。
5. `0x2:0x8` 定名：`BridgeSyncFreeEvent`（语料 `FUN_00192930(param_1,2,8,…)`，
   decompiled.c:12324；IN/OUT 各 4B 与活体一致），位于 destroy 收尾，非致命。

## 推断与下一步（下一轮）

- `0x2:0x8` 实现（SYNC 组 stub-ok 一行，IN 16→此处 IN 4/OUT 4；与 0x2:0x1/0x2:0x7
  同模型，值不被回读）+ 门禁 + 活体复验（含本轮 `b5*` 结论复验）。
- 另：`b5` vs `b5*` 的 legacy/DDK2 差异应记入 harness 用法（legacy 取 `b5[1]`，
  DDK2 取 `b5[0]` 指向的结构；两者碰巧都是 devmemctx 指针，但间接层级不同）。
- 遗留：79+ 提交未 push；`r135` jsonl 未动；会话已失（重启），重建待下一轮一并做。
