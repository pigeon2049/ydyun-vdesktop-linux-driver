# r191：kill-while-busy 关账轮——mid-maps 击杀亦 Δ0（批准执行）

- **结论**：GDB 监督下在第 104 个 mmap syscall 处击杀真实 blit
  （trace 证实死时 3 MAPs live、0 UNMAPs、4 mmaps/2 munmaps），
  事后 probe 66→66、bridge 1→1（Δ0），无 D 态，L3 复绿。
  kill-while-busy 亦不触发 abandon——r183 的 file_release
  early-return 假设至此无活体支持，+65 根因仍未命名（见收敛边界）。
  本轮 7 活体轮（含 GDB×4）零新增泄漏（66 全程稳定）。

## 实测（批准内活体；无重载——`rmmod` 被 infra 持有挡回，见边界）

1. 基线：probe 66 / bridge 1；无 `mt_live_*` 在载；`/tmp` 2%。
2. `rmmod mt_pvr_bridge` → `in use`（EBUSY，无状态变化）：
   `/dev/dri/renderD128` 被本会话工具链（`ai.opencode.des`、
   `opencode-cli`）持有，不能杀——`=2` 重载路暂不可行。
3. blind `timeout -s KILL 3`：blit 0.188s 内干净 SIGABRT，
   KILL 未触发（Δ0，意料之中；遂转 GDB 定点）。
4. GDB 定点教训两则：①首轮 `catch syscall mmap` 未初始化计数器，
   ②绝对计数在有/无 shim 下漂移（shim 增 startup mmaps）、
   `$r8==3` 条件受 fd 分配影响——均如实记录，未藏拙。
   最终：`set $n=0` + 无条件计数至 #104（总 ~110）处 `kill`，
   进程死时 3 MAPs live（trace 点名）。
5. 事后：probe 66 / bridge 1（Δ0）；D 态仅 jbd2/kworker；
   `pvr_node_probe renderD128` 0 failing / 0 mismatch。
6. 证据：`r191-killbusy.jsonl`（8737 行击杀轮 trace）；
   GDB 脚本见下（/tmp 易失，此处留档）：
   `set $n=0; catch syscall mmap; commands(silent; $n+1; if>=104:kill,quit; continue); run`。

## 推断收敛（+65 仍未命名，但边界已收紧）

- 已排除：maps 本身（r184 Δ0）、干净 abort（r184/r191 ×3 Δ0）、
  mid-maps 击杀（本轮 Δ0）、prepare 失败记账（代码配平 r183）、
  残留进程/fd（全轮无）。
- 剩余候选：`=2` 路径特有瞬态（重载被挡，不可验）、r182 前多轮
  累积误归因、非 bridge 持有者。释放语义继续冻结。
- 积极面：7 活体轮零新增泄漏，会话健康 freeze 可持续。

## 边界

- 无 GPU 提交（legacy `-25` 拒绝）；无模块重载；
  translator 未碰；GDB 监督 4 轮，会话零异常。
- 本轮无代码改动，门禁数不变（295+292）。

## 下一步（候选）

- 解 infra 持有后重开 `=2` 轮（kill-while-busy 的 `=2` 版 + update 验证）；
  或 TQX 真发射（独立批准）。
