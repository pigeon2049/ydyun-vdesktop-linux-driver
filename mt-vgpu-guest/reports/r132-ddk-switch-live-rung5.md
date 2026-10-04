# r132：`ddk_feature_set=2` 活体 rung5 与默认逐调用一致——开关本身不足以改变 UMD 路径

用户批准 live（先自行释放 Chrome 对 renderD128 的占用，引用数 0 后才动手）。

## 实测
- rmmod 旧桥 → `insmod ... ddk_feature_set=2`（参数读回 2）→ passthrough rung5
  （connect→device→memctx→render ctx→CreateSyncPrim），无 timeout，全部 `-> 0`，退出码 0。
- 再 rmmod → 默认参数 insmod（读回 0）→ 同一条链。
- 两份 trace 的桥调用直方图 **完全一致**（89 = 89，逐 id:cmd 计数无差）。
  证据：`r132-ddk2-rung5.jsonl`、`r132-default-rung5.jsonl`。
- 事后：dmesg 无新增 WARN/BUG/Oops，无 D 态，引用数 0/113，模块已回默认参数。

## 结论
- 实测：该链上写入 `features+0x54=2` 不改变 UMD 的桥调用序列，rung5 无崩溃。
- 推断（未测）：r78 的判据是"非零 CCB create 之后出现 SyncPrim/SubmissionBuf 调用"，
  本轮没跑那一步（命令在 r78 记录里未留全，我没有凭猜重构）；也可能 UMD 读的不是
  这个偏移，或需要同时改别的特性字段。DDK2 是否可达仍**未确认**。

## 下一步
找回/重建 r78 的非零 CCB create 命令，在 `ddk_feature_set=2` 上重跑并比较其后有无
SyncPrim/SubmissionBuf 桥调用（需再次批准重载）。
