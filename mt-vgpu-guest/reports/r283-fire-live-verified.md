# r283：独立模块首次真发射全绿（批准执行）——21 块/1310720 像素逐块验过

- **结论**：`mt_live_tqx_fire` 在自有 `card2`/`renderD129` 上 bring-up（`prepared=1`、`slices: ready cores=1`）后单发 21 块 fill，21 fence 逐个 signal，全 1310720 像素逐块读回比对 `0xff0000ff` 零 mismatch（`fired=1 chunks=21 verified=1 bad=0/1310720 result=0`）。拆模块干净（节点消失，refs 回 bridge 1/probe 1），本轮窗口零新增 WARN/BUG/Oops。**Freeze 已恢复（bridge/probe/renderD128 全程未碰）。**

## 实测（执行过）

1. 预检：仅 bridge+probe 在载；节点仅 `card0/card1/renderD128`；`/` 27%；盘内 `.ko` vermagic 对版含 `mtlivefire`。
2. `insmod enable=1` 首拒 `-EBUSY`（排他门大声失败，设计内）：诊断行 `busy: ... completed=0 ... addr_objs=0 buf_objs=0` 唯一定位 `completed=0`——`completed` 是终身计数，新会话首跑恒零（向 `live_3d_drm`/bridge 对齐：二者均不以此为门；5s fence 预算 + fail-fast 覆盖未验证的完成路径）。改代码 + 门禁（`test_no_prior_completion_gate`），重载即过。
3. `run=1` 首跑：块 0 prepare 即 `-EBUSY`——pool slices 随提交外借、完成才归还，全部 prepare 再发的流水线自我阻塞。改串行流（prepare→submit→等→验，逐块），门禁同步（`test_serialized_chunks`）。
4. 二跑：块 0 fence 正常 signal（完成路径在新会话成立），读回 `-ENODEV`——verify 段漏置 `upload_dev`。补 `WRITE_ONCE` 包裹，重载三跑即全绿（`finished` 行见上；`first=last=0xff0000ff`）。
5. 恢复：`rmmod` rc=0，`card2`/`renderD129` 消失，refs 回 1/1；5 条 WARNING 全系 10-07 旧转储（r275 签名），窗口内 0 新增。证据 `reports/r283-fire-live.dmesg`（0600，30 行窗口）；暂存区已清空。
6. 门禁：`check-offline` 366 Python + 292 C 全绿；`make kernel` W=1 零警告；反向验证通过（删 `dma_fence_wait_timeout` 即 2 FAIL，还原即绿；首轮 `_X` 后缀变异因子串包含未触发，作废——r280 同教训）。

## 边界

- 全帧内容从不同时驻留：每块在 scratch 基址逐块验证（比 r280 尾块验证更强），跨块一致性由 21 fence 信号 + 全量读回覆盖。
- `attempted` 单发语义：同一次加载只打一轮；复打需重载模块。
- bridge 内 r280 分块 fire 仍在盘未上机（renderD128 不可重载现状未变）；合并入桥待另议。

## 下一步（候选）

1. 合并策略（离线 recon，要不要把 fire 合进 bridge / 保持独立模块）；或
2. 更大矩形/多轮 soak（独立模块重载即可，无需碰 bridge）。
