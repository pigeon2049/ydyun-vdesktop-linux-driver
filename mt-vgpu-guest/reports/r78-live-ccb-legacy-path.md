# r78：活体非零 CCB create 仍走 legacy 路径（桥故意为之，DDK2 需特性开关）

用户批准的单次 live 实验结论：passthrough 非零 CCB create
（pack `0x0733`，活桥 `0x88:0x0 ret=0`）之后**没有任何 SyncPrim/
SubmissionBuf 桥调用**，DDK2 照旧崩于 `+1490`——因为我方桥在
`mt_pvr_device.h:143-145` **故意**把 `features+0x54` 钉在 2 以下，
UMD 遂选 legacy 分配路径。这是 bring-up 期验证过的刻意选择，
不是 bug。DDK2 可达 = 新 DDK 特性广告 = 桥行为变更（改代码 +
重编 + 重载模块，另需批准），不是再跑一次 harness 能解决的。

## 实测（6 次 passthrough，崩溃点一致，无残留）

- `0x88:0x0 IN=…33070000 ret=0`（非零 pack 活体生效）；
  其后 0 条 SyncPrim/SubmissionBuf 相关桥调用（90 条记录以 create 收尾）。
- DDK2 崩溃 RIP 与 fabricated 完全一致（`+1490`）；
  dmesg 用户态记录 `segfault at 48, error 6`（写 NULL+0x48）——
  连 fault 地址都与归因吻合（此前"at 8"的是 render flake，本次是 DDK2 写 fault）。
- 会后零残留：`pending=0 completed=23`、`objects=34`（一字不差）、
  引用 38/0、D 态 0、WARN/BUG/Oops 无新增（仍 6 条旧行）、
  `arena close fallbacks=0`（崩溃进程的按文件对象随 close 释放）。

## 决策含义（给 STATUS 下一步 1）

- update 侧（DDK2 形状）已从"缺输入"转为"需桥特性开关"：
  `features+0x54 >= 2` 广告 → UMD 走 gated create → `+0x18/+0x28`
  存活 → DDK2 可达。这是一次有风险的桥行为变更（新分配路径未经验证，
  且要重载活会话模块），必须单独立项、单独批准，不在本轮。
- 在开关打开之前，check 侧（r72/r73：legacy 路径全绿）就是翻译器
  输入规约的完整可用部分；T3（DM 队列）仍被 CCB 内容卡住（STATUS 原判不变）。

## 证据

`reports/r78-live-ccb-legacy-path.jsonl`（90 条，`0x88:0x0` 非零 pack +
其后无 SyncPrim）；`mt_pvr_device.h:143-145` 注释即设计意图。
