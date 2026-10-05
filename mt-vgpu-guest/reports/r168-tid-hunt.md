# r168：trace 加 tid——排除工作线程交错，崩溃为单线程确定性路径上的时序/环境敏感

- **结论**：shim 全部 25 处 trace 记录加 `tid`（`SYS_gettid` 裸系统调用；门禁断言存在且 `>0`，反向注零验证可抓获）。活体对比：失败轮与 GDB 通过轮均为**单 tid**——r167 的“工作线程交错”假设被证伪。失败轮桥调用序列是成功轮的严格前缀（65/65 一致，全 ret=0）。ASLR 关、单 CPU、`MALLOC_PERTURB_` 0/165 对照仍全崩；GDB 4/4 过。发布者仍未命名，但搜索空间收窄到单线程时序/环境敏感。会话健康，freeze 继续。

## 实测

1. shim 改动（`probe/umd_bridge_shim.c`，用户态、零硬件风险）：新增 `umd_tid()` + 25 处格式/参数配对；`git diff` 经归一化确认纯 tid 改动。门禁：`check-offline` 269+272 全绿；`test_pvr_shim_ccb_resolve` 加 tid 断言；反向将 `umd_tid()` 注零 → 测试 FAIL，恢复 → PASS。
2. 活体（批准执行，手跑，桥零重载）：rung5 配方 standalone 失败轮（单 tid `13235`）vs GDB 通过轮（单 tid `13282`）。
   - 桥调用前缀 65/65 一致（`0x6:0x20`×15、`0x6:0x11`×15 为 15 堆明细/创建；PMR 序列中断于第 3 组 map 后）。
   - 通过轮继续走完 `0x82:0x8 → 0x1:0x4 → 0x2:0x0 → 0x6:0x3/0x6 → 0x2:0x7`（CreateSyncPrim 全链）。
3. 对照组：`setarch -R`×3 全崩；`taskset -c 0`×3 全崩；`MALLOC_PERTURB_`=0/165 各×2 全崩。故 ASLR、SMP、fresh-heap 填充三变量已排除（perturb 只覆盖 fresh 页，复用堆不在此列——证伪力度限于 fresh 堆）。
4. 会话健康：probe ref 1，bridge ref 0，节点在位，桥错误计数 0。

## 边界

- tid 只证明“无第二线程碰桥”；UMD 内部非桥线程（如纯计算线程）不在此证伪内——但崩溃点是桥 OUT 组装的对象，桥调用全同，故内部线程参与的可能性低。
- 发布者未命名；rung6–8 仍被阻；`translate_kick` 仍 off；无 GPU 工作提交。

## 下一步（候选）

- 单线程 + 同输入 + 同桥响应而结果不同 → 剩余变量：PID/时间等进程内熵、复用堆历史。候选：对 `r12+8` 的发布调用（`0x6:0x11`/`0x6:0xf` OUT 句柄 → UMD 句柄表）做 GDB 条件断点，比对通过/失败轮的句柄值。
