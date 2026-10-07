# r299：bump 满足 UMD，越过 submit3（批准执行）——像素比对是下一关

- **结论**：同窗口热换（NULL 跳过修 + 门禁）后复打：`submit3 bump: update=2 first_sync=0x1029 first_off=0 first_val=1`（awaited value = **1**，fence 语义实锤），UMD 的 `SyncPrimWait` 即时满足——`Submit transfer command OK → Wait for blit to complete OK`，随后 `*** Output does not match source *** Test FAIL`（exit=1，干净退出，无 hang 无 abort）。trace 8290 行（+89 行 hang 后续：`0x2:0x1`、销毁、断开全序）。**UMD 越过 submit3 系首次；剩余缺口精确收敛为像素执行（fire 写 scratch 而非 UMD 目的池）。**拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：standing 授权（同一窗口内热换重打，用户未干预）。诊断行定位 `entry 1 sync=0x0`（update 数组第二项是 NULL 填充）；修：双遍均跳过零柄（`if (!handles[i]) continue`，kcalloc 零初值保证安全）+ 门禁 1 项；`check-offline` 全绿；`make kernel` W=1 零警告；`rmmod` 三开 → 新构建三开 + bump（桌面全程未动，窗口完好）。
2. 复打：bump 成功行如上；UMD 日志逐行：建链 OK → 建 transfer 上下文 OK → 提交 OK → 等待完成 OK → 像素比对 FAIL → 断开 OK。exit=1。
3. 证据：`r299-bump-unblocks.jsonl`（0600，8290 行/178 bridge 调用，末四条 `0x2:0x1 → 0x6:0x7 → 0x1:0x10 → 0x1:0x1` 全序拆除）；暂存区已清空（含 r298 遗留）。
4. 恢复：拆桥 + 默认 + L3 双绿 + 拉回（10 进程）；refs 1/1。

## 边界与下一步

- 像素缺口是执行问题，不是同步问题：fire 的 fill 程序是对的（r283–r287 全像素闭环），只差把 dest 从 scratch 换成 UMD 池（`locate_dst` 已解出 dst PMR/rect——正是 dry-run 的输入；输出侧是 TQX fill，链条齐备）。**下一步 = fire-into-destination（离线改代码 + 窗口验）：UMD 的 `Test PASS` 即真实绘制像素闭环。**
- CCB 本轮 `nonzero=40`（第 14 轮值 `a0 6c`），略。
