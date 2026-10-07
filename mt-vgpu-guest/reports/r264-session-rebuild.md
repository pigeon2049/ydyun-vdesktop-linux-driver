# r264：重启后活会话重建（批准执行）——r211 流程复用，L3 全绿

- **结论**：r263 死锁重启后按 r211 流程重建：`cold_disconnect finish=0/1` 双 clean（rings idle，`guest=0 firmware=1`）→ `fresh-trial.py --run --runtime-context` rc=0，新 trial `20261007T131341Z-3b9ae877`（`guest=2 firmware=2`，published/connected/pinned=1，probe ref 1）→ `mt_pvr_bridge.ko` 默认参数加载（`card1`/`renderD128`，ref 0）→ L3 全绿（node 0 failing/0 mismatch，dma smoke PASS refs 平衡，无 GPU 提交）。dmesg 零 WARNING/BUG/Oops。**Freeze 即刻生效。**注意：在载桥是 r263 含死锁构建（盘内最新），默认参数下 slices 路径不执行（`translate_tqx_ctx` off），死锁代码休眠无影响。

## 实测（执行过）

1. 开工预检：uptime 2min（重启确认）；无 `mt_*` 模块；`00:0e.0` 无绑定；`/ 27%`；`make kernel/recovery` W=1 零警告；dmesg 打 `[r264] rebuild-start` 标记。
2. cold 0/1、`fresh-trial`、`insmod` 默认桥、L3 均如上。
3. 终态：probe ref 1，bridge ref 0；`/dev/dri` = `card0` + `card1`/`renderD128`。

## 边界

- 本轮只做重建 + L3，未跑 L4、无 UMD 流量、无 GPU 工作；`translate_kick` 保持 off。
- r150 Oops 未复现但根因未命名（口径延续）。
- **Freeze**：不 rmmod、不 unbind、不提交额外工作；`make probe`/`make umd` 继续禁用。

## 下一步（候选，需批准）

1. 锁序修复（离线）：slices 的 prepare 移出 trial_lock 或 buffers 锁内化 + 锁序门禁。
2. slices 重验（批准执行）。
