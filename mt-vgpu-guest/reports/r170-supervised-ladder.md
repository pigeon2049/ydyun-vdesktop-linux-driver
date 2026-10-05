# r170：GDB 监督下 L4 八级全绿（rung5–8 打通；批准执行）

- **结论**：`gdb -batch -ex run`（零断点、仅换监督者）下逐级跑通 rung5 syncprim、rung6 kicksync 建销、rung7 compute 建销、rung8 `RGXKickSync → 0`（accept-and-inspect，即时 fence，无 GPU 工作；`translate_kick` 仍 off）。四轮 trace 逐调用 ret=0（125/127/124/127 行）；probe ref 稳 1；dmesg 无新增 WARN/BUG/Oops。方法非常规，效果与监督方式无关（同系统调用序列、同桥），如实记录。会话健康，freeze 继续。

## 实测

1. 监督方式：`gdb -batch -x script`（`file` + env + `run ARGS` + `kill`），脚本内无断点、无单步；UMD 侧行为与直接执行一致（5/5 通过率，r169）。
2. rung5：`CreateSyncPrim → 0`，EXIT-OK。rung6：`RGXCreateKickSyncContextCCB → 0` + destroy → 0。rung7：`RGXCreateComputeContext → 0` + destroy → 0。rung8：`RGXKickSync → 0`（84B 全零 IN，inspect 路径）。
3. rung8 trace 存 [`r170-ladder8-kick.jsonl`](r170-ladder8-kick.jsonl)（127 行；rung5–7 trace 在 `/tmp/opencode/umda/l4-sup{5,6,7}.jsonl`，未入库）。
4. 会话健康：probe ref 1，bridge ref 0，节点在位；arena-close 均为常规释放行（`fallbacks=0`）。

## 边界

- GDB 遮罩机制仍未解释——本轮是利用现象而非理解现象；rung5 standalone 崩溃仍在， ladder 常规跑法仍不可用。
- `RGXKickSync` 的 0 是 inspect 回包，不是 GPU 执行；真提交入口仍 `-ENOTTY`（S4 边界）；无 GPU 工作提交。

## 下一步（候选）

- 同一监督方式跑 DDK2 `drm_major=2` 全链（r141–r143 配方）复验；update 数组活体（check/update 非零 kick 的 inspect  inventory 已在桥侧，r63）。
