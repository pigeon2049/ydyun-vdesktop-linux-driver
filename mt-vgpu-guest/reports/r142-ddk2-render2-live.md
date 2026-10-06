# r142：DDK2 render 建销落地——`=2` 下 render 首返 0，下一堵墙是 CCB2（批准执行）

- **结论**：`0x82:0x12`（建，12-in/12-out{handle,error}）+ `0x82:0x13`
  （销，复用 widened-handle 释放路径）+ `0x2:0x2`（SyncPrimSet，stub-ok 零回）
  落地后，`=2` 同链 `RGXCreateRenderContext → 0`（此前 37），`CreateSyncPrim → 0`。
  链随后死于 `0x88:0x5`（CreateKickSyncContext2，仍 `-ENOTTY`）；
  UMD 在缺失处继续解引用空态直接段错误（用户态， harness exit 139，内核零异常）。
  桥已恢复默认并 freeze（ref 8/0，L3 全绿，dmesg 零 WARNING）。
- 零 timeout；未提交 GPU 工作；probe 未动。

## 实测（执行过）

1. 新桥（`=2`，读回 2）：r133 同链 → render **0**、syncprim 0、CCB 37、中止；
   轨迹唯一非零：`0x88:0x5`（in 8/out 12，`-ENOTTY`）。
   证据：`/tmp/opencode/umda/r142d.jsonl`（易失）。
   注意：`0x2:0x2` 在 render 期（seq~113）与收尾各出现一次，均已回 0。
2. 用户态段错误定性：`segfault ... in libsrv_um_MUSA.so`，内核侧无 Oops/BUG/
   WARNING（显式 grep 为空），模块与会话完好（arena 正常 close，
   `fallbacks=0`），属 UMD 缺命令后的空解引用（与 r 期 ZSBuffer 冒充段错误同类）。
3. 恢复：桥重载默认（读回 0/0），`pvr_node_probe renderD129` 0 failing，
   ref 8/0。

## 实现与门禁（执行过）

- `mt_pvr_wire.h`：`mt_pvr_render2_create_in/out`（IN `{hPrivData, ui32Priority}`，
  沿用 2.7.1 头字段名；OUT `{handle, error}`）+ static_asserts 12/12。
- `mt_pvr_bridge.c`：`pvr_cmd_render2_create`（KIND_CONTEXT 对象模型，IN 仅验尺寸，
  与 legacy 一致）；dispatch 加 `0x12`/`0x13`；`0x2:0x2` 进 stub-ok 行列。
- 需求表 `(0x82,0x12)` 行：`umd_output_size` 12（头与活体一致，无分歧），
  补 `observed`（r142c 轨迹，in/out [12]/[12]）。
- 门禁：新增 `test_pvr_ddk2_render2.py`（路由×3；反向验证掐掉即红 2 项——
  注意：反向验证中曾误 `git checkout` 整文件，一度丢实现，已原样重打并以 diff 核对，
  教训：反向验证只许 sed 单行，禁整文件 checkout）；
  wire sizes / requirements 全绿；`make kernel` W=1 零警告（仅桥重编）。
- 自我订正：r141 称 OUT 为 4（误读 stub `&local_14,4`）；活体 render 期调用点
  实为 out_size=12，已按活体修正，r141 的 4 字节说法作废。

## 下一轮

- `0x88:0x5` CreateKickSyncContext2（IN 8/OUT 12）及其 destroy 对端；
  随后才是 r134 预言的 CCB 期 SyncPrim/SubmissionBuf 分配器。
- 遗留：77+ 提交未 push；`r135` jsonl 未入库文件仍在，未动。
