# r143：DDK2 CCB 建销落地——create 回 0，链进分配器后 UMD 空解引用（批准执行）

- **结论**：`0x88:0x5`（建：8-in/12-out `{handle,error}`，KIND_KICKSYNC 对象模型）
  + `0x88:0x6`（销：复用 legacy 释放路径）落地后，`=2` 同链走得更深：
  render 0 → syncprim 0 → CCB create 桥调用回 0 → UMD 随即在
  `SubmissionBufAlloctorCreate` 空解引用（gdb 活体 backtrace：
  `RGXCreateKickSyncContextCCB → SubmissionBufAlloctorCreate → SIGSEGV`，
  与 r142 同一 `.so+0x94a9c` 崩溃点）。内核零异常，会话完好。
  这正是 r134 预言的门控下游（SyncPrimAlloc + SubmissionBufAlloctorCreate），下一轮。
- 零 timeout；未提交 GPU 工作；probe 未动；桥恢复默认 freeze（ref 8/0，L3 全绿）。

## 实测（执行过）

1. 新桥（`=2`）：r133 同链 → render/syncprim **0**，CCB 期唯一桥调用
   `0x88:0x5` **ret=0**（in 8/out 12），随后 harness exit 139。
   证据：`reports/r143-major2-ccb2.jsonl`（已入库；116+ 调用零非零）。
2. gdb 跟随子进程：崩溃栈只有三帧（UMD 内 + harness main），
   无桥调用、无内核参与——崩溃在分配器初始化逻辑，不在桥应答。
3. 恢复：桥默认（0/0），node probe 0 failing，ref 8/0，dmesg 零 WARNING。

## 实现与门禁（执行过）

- `mt_pvr_wire.h`：`mt_pvr_kicksyncctx2_create_in/out`（8/12）+ asserts；
  `0x88:0x6` 复用既有 8-in/4-out 形状，无新结构。
- `mt_pvr_bridge.c`：`pvr_cmd_kicksyncctx2_create` + dispatch `0x5`/`0x6`
 （destroy 与 legacy 共 KIND_KICKSYNC 表）。
- 需求表 `(0x88,5/6)` 行：命名（decompiled stub 名）+ observed（create 有活体 1 次；
  destroy 尚未活体执行，记 count 0）。
- 门禁：新 `test_pvr_ddk2_kicksync2.py`（路由×3；反向单行掐断即红，已还原并
  diff 核对）；命名集合 pin 更新（0x88:0x5/0x6 除名，0x88:0x7 留白）；
  wire/requirements 全绿；`make kernel` W=1 零警告（仅桥重编，.ko 符号已核对）。

## 下一轮

- `SubmissionBufAlloctorCreate` 的输入：其参数（render 上下文 + 同步原语 + CCB 描述）
  由哪次桥调用提供、在哪一步变空——先只读追踪（UMD_DUMP_BRIDGE
  定向 + 语料核对），再决定补哪块（SyncPrim 分配器命令嫌疑最大）。
- 遗留：78+ 提交未 push；`r135` jsonl 未动。
