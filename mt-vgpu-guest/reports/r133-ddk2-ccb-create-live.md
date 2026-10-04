# r133：非零 CCB create 在 `ddk_feature_set=2` 下仍与默认逐调用一致——写 +0x54 没有改变 UMD 分配路径

用户批准（引用数 0、无进程占用后动手）。零 timeout，无 kick。

## 实测
- 链 = rung5 + `RGXCreateKickSyncContextCCB b7* b5 u0 u0x33 u0x07 u0 b12`
  （pack `0x0733`，命令按 r76/r77 记录还原）+ `RGXDestroyKickSyncContext`。
- 先 `ddk_feature_set=2`（读回 2），再默认 0（读回 0），各重载一次、各跑一遍：
  全部 `-> 0`，退出码 0；桥调用序列 91 = 91，**逐项一致**（无新增 SyncPrim/SubmissionBuf 调用）。
- 事后引用 0/113，dmesg 无新增 WARN/BUG/Oops，无 D 态；模块停在默认参数。
- 证据：`r133-ddk2-ccb-create.jsonl`、`r133-default-ccb-create.jsonl`。

## 结论
- 实测：在 rung5 与非零 CCB create 两条链上，`features+0x54=2` 都不改变桥调用序列。
- 推断（未测）：UMD 判门控读的不是我们写的位置/值——候选：偏移基址（桥写在
  `file->features + 0x620 + 0x54`，UMD 经 `conn+0xa0+0x620` 取）、`>=2` 之外还有别的特性位、
  或门控另有来源。需回语料核 `RGXCreateKickSyncContextCCB@0x52180` 的门控读取（离线，先核 SHA）。

## 下一步
离线：按 AGENTS §9 查语料，确认门控究竟读哪个字段；不要再盲目重载。
