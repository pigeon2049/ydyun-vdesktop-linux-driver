# r184：defaults 活体差分 Δ0——maps 无罪，+65 与 prepare/挂起强相关（批准执行）

- **结论**：defaults 下真实 `musa_blit_test -device 0 -f -o`
 （passthrough 记录、无重载、无 GPU 工作）走 legacy
 （`0x89:0x0 → -25` 后 SIGABRT，exit 134，无 `0x88:0x4`、无
  prepare、DDK2 零调用）：9 maps / 11 mmaps / abort / close 后
  probe 66→66、bridge 1→1（Δ0），无 D 态，会话健康。
  “有 map 无 prepare”不漏——r183 的 file_release early-return
  假设被修正为条件触发式（见下），而非无条件 map 泄漏。

## 实测

1. 基线（只读）：probe 66 / bridge 1；无 `mt_live_*` 在载；
   `/dev/dri` 有 `card1`/`renderD128`；`/tmp` 2%。
2. 本轮（批准内单次活体）：`UMD_SHIM_PASSTHROUGH=1` +
   `LD_PRELOAD=build/probe/umd_bridge_shim.so`（与源码同代已验证），
   `UMD_TRACE=/tmp/opencode/r184-defaults-blit.jsonl`，
   `timeout -s KILL 100` 外层（r158先例；进程实际秒级 SIGABRT，
   未触发 KILL）。UMD 双份 SHA 均为 `b3058c02…`（在用版本）。
3. 事后：probe 66 / bridge 1（Δ0）；D 态 0；trace 8205 行。
4. trace 解码：`0x89:0x0 ret=-25` ×1（legacy 拒绝，
   `0x82:0x12`/`0x88:0x5` 零出现——defaults 事后确认）；
   `0x6:0x13` ×9（maps）、`mmap_real` ×11、`munmap` ×8、
   `0x6:0x14/0x16` ×4（有序部分拆）；
   `ioctl_real fd=-2 req=0xc0406400 ret=-9` ×4032 系 open 前
   DRM 版本探测（EBADF chatter，无害）。
5. 附带只读发现：`/dev/dri/renderD128` 的 2 个持有者是
   `ai.opencode.des` 与 `opencode-cli`（本会话工具链的节点枚举，
   无 PMR、无 ref 含义）；bridge Used by=1 归属未深究，不碰。

## 推断（差分逻辑；`kill-while-busy` 精炼假设，未验证）

1. Δ0 证明：同 maps 量级下，无 prepare 的 abort+close 全配平。
   于是 =2 轮的 +14/轮不能由 maps 本身解释，必含 prepare 路径
   或挂起态的贡献。
2. r183 假设的修正版（仍是推断）：file_release 的双 early-return
   是**条件触发**——destroy 失败需 `active_uses || owners`
   （`mt_gpu_vm.h:327`）或 preflight 失衡。=2 轮 UMD 是
   hanging 后被 KILL（r182），kill-while-busy 正命中该条件 →
   abandon 整文件 maps；本轮系干净 SIGABRT、无在飞工作 →
   destroy 成功 → Δ0。两者行为差与触发条件差同向。
3. 未命名余量：66 中 +65 的逐轮归属仍无 dmesg 对账（本容器无权读
   dmesg）；bridge Used by=1 的持有者；`+23` 首轮中超 14 的部分。
   释放语义本轮未动（红线继续有效）。

## 边界

- 无 GPU 提交（`-25` 拒绝）；无模块重载； translator 未碰。
- 本轮未复现 +65（这是好消息：干净 abort 不漏），也没有证实
  kill-while-busy 链（需故意挂起再杀的一轮，另立项待批）。

## 下一步（候选，按序）

1. 若要关账 +65：故意 kill-while-busy 一轮并前后采样
   （before-maps / after-maps / after-kill / after-close），待批准。
2. TQX 真发射（submit+fence+落位+像素回读）仍排在归因之后。
