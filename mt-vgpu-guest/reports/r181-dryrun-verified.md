# r181：transfer dry-run 活体验证——内核程序字节与离线预言一致（批准执行）

- **结论**：`translate_transfer` dry-run（默认 off；pool 解析→rect→`mt_tqx_fill_build`→digest，不提交）两次 `=2` 窗口实测：首轮暴露选择 bug（CCB 自身 PMR 以更大像素胜出，rect 错配 honest `-EINVAL`→UMD 中止），修复为“排除 CCB PMR + 非零最多”后，次轮 `pool=0x1032/color=0xff0000ff/va=0x8000a43000/1280x1024/fnv=0xd893618ca42d3711`——与离线同 builder 预言的 `0xd893618ca42d3711` **逐位一致**。输入→程序映射活体验证成立；另顺手修 `mt_tqx_destination_input` 栈未初始化（fresh 栈掩盖，门禁钉死）。桥恢复默认 + L3 复绿，freeze 继续。

## 实测

1. 代码：`translate_transfer` 参数 + `pvr_submit3_transfer_dry_run`（VA 定界复用 observe；`w*h` 错配、`-EOPNOTSUPP` 歧义、`-ENOENT` 无绑定皆大声失败）；`make kernel` W=1 零警告；278+292 全绿；反向（默认翻转/路由掐断/乘积掐断）皆可抓。
2. 附带修：`mt_tqx_fill.h` 的 `dest` 未初始化（离线预言机首次 `-EINVAL` 暴露；live_3d 靠 fresh 零栈幸存）。C 确定性门禁 + python 结构门禁双钉。
3. 活体（两次 `=2` 重载，均 ref0，probe 零触碰）：真实 blit → observe  digest → dry-run 行；UMD 在回 0 后 hanging（无执行无 fence，预期内，timeout 终结）。
4. 恢复：默认重载 + L3（node 0 failing，smoke PASS）+ dmesg 无模块 WARN；probe ref 1、bridge ref 0。

## 边界

- dry-run 不提交：GPU 执行、fence、像素回读一概未发生；digest 一致只证明“程序字节正确”，不证明“打得动”。
- 池选择是原型启发式（最大非零）；黑 fill/多表面仍 `-EOPNOTSUPP`（已声明）。
- dims 仍是调用方 1280×1024（错配会大声失败，r179 门禁语义）。

## 下一步（候选）

- 真发射：scratch BO + TQX fill 提交 + fence + 落位 + 像素回读（r180 规约），`=2` 窗口。
