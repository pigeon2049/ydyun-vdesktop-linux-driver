# r187：预设值复核（第二轮）+ bridge 审计结论 + 重构（零硬件触碰）

- **结论**：第二轮扫面覆盖 bridge 全部裸字面量与全部 19 处
  `WARN_ON`、全部锁点与全部 dispatch 分支。可动作的两项已重构
  （PCI 槽位宏统一、0x88 功能号命名），其余 6 类经审计为故意保留
  （见下）。门禁 290+292 全绿，`W=1` 零警告；反向验证通过。

## 预设值复核（第二轮）

1. 已抽取 R1：`PCI_DEVFN(14, 0)` 三处裸写并存宏定义
   （`MT_PVR_PCI_DEVFN`）——两处 lookup 改引宏，字面量全树仅剩
   定义处一例（门禁钉死恰一次）。
2. 已抽取 R2：`kicksync_submit` 内 5 处裸 `function == 0x2/0x3/0x4`
   无注释——`mt_pvr_wire.h` 新增 `MT_PVR_FN_KICKSYNC{2,PROP,3}`，
   与线缆注释同名，dispatch case 标签不动（已有注释）。
3. 故意保留（审计结论，不抽）：
   - dispatch 全部分支标签：每 `case` 均有协议名注释，
     每 `switch` 均有 `default: -ENOTTY`（含外层），改宏无收益；
   - ioctl 号：三处各有 `static_assert` 漂移断言；
   - module_param 默认：静态存储天然 0/false，且逐个有
     `MODULE_PARM_DESC` 说明；
   - PMR 尺寸（`0x1000`/`0x2000`）：sync/CLI/USC 上下文自明；
   - `features+0x54`：代码零出现（r134 后门控即 `drm_major`），
     仅存于注释与 param 描述；
   - stream 端点/slot_va/readback 除数：异语义，r186 已定界。

## 代码审计（一轮；`mt_pvr_bridge.c` 全文件走查 + grep）

1. `WARN_ON` 19 处：全部位于 teardown/release-and-continue 路径
   （失败仅告警、流程继续），无热路径误用——可接受，不动。
2. `pvr_file_release` 双 early-return：r183/r184 已有归因在途，
   红线冻结释放语义——不动。
3. 锁序：`translator_lock` → `trial_lock` → `file->lock` 全树一致
   （`prepare`/`translate_kick`/`exit` 同序，注释在位）——不动。
4. `(void)` 占位 4 处均有文档句——不动；TODO/FIXME 零命中；
   死 static 函数零（`W=1` 干净即证）。
5. 方法论自检：r186 碰撞检查曾被 `head -3` 截断误导；
   本轮碰撞/残留检查一律全量输出（`grep -c` + 点名），不再截断。

## 实测

- `make check-offline`：290 Python + 292 C 全绿
  （新增 kicksync_fn 2 项、session_ops 槽位宏 1 项）。
- `make kernel`（`W=1`）：零警告零错误（含 touch 重编）。
- 反向：`MT_PVR_FN_KICKSYNC3` 改 `0x5` 即红，还原即绿；
  session_ops 槽位宏测试误报一例（宏定义自身含字面量），
  已修正为“恰一次”断言——测试修测试，如实记录。

## 边界

- 纯重命名/宏引用重构，数值逐位不变（门禁钉死），无行为变化；
  未碰模块加载与会话。

## 下一步（候选）

- 活体项仍待批：kill-while-busy 关账 / TQX 真发射 / `=2` update 验证。
