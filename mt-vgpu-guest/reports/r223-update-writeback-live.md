# r223：update 写回活体验证（批准执行）——update-only fire 写值，check 计时读回

- **结论**：translator 的 update 写回语义（r159）在活体闭环：新工具 `pvr_update_writeback` 在 `translate_kick=1` 桥上，Step1 发 update-only kick（check=0/update=1，真 sync PMR，off 0，值 1）回 0；Step2 发 check kick（同槽同值）**0.11ms** 即时通过——时间即读回，无需 PMR mmap。dmesg 双行 `check=0 update=1 tag=1 fence=5` → `check=1 update=0 tag=2 fence=6`（200µs 相隔，fence 序列延续）。probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r223] update-writeback-start` 标记。关键判断：默认桥下 update fire 只走 inspect（假阳性），必须 `translate_kick=1` 重载——已执行（ref 0，probe 未碰）。
2. 工具（离线部分）：84B 包字段逐名取自 wire.h；teardown 销 context（PMR/对象随文件释放，同 kick_probe 惯例）；`-Werror` 零警告构建；门禁 5 项（fire 接线/回 0 期望/check 接线/4s 计时断言/teardown + wire struct）。
3. 反向验证：删计时断言 → 门禁 FAIL；还原 → OK，工具重编。
4. 活体：5 项全 ok，exit 0。事后 refs（bridge 0 / probe 25，translator 持有）；拆桥 `unloaded cleanly` 后 probe 25→1；默认桥 + node/smoke 全绿；终态 1/0；dmesg 零 WARNING/BUG/Oops。
5. `check-offline`：323 Python（318+5 新）OK；C/内核沿用 r222（本轮零改动，未重跑）。

## 边界

- 证明的是 translator 写回链（marker → fence → PMR 写值 → 后续 check 可见），不是 UMD `SubmissionSetUpdateSyncPrim` 的数组编组（仍待 producer）；值是工具手塑 V=1，不是 UMD 生成的 `flag&2` 项。
- 本轮 fresh file 自包含 + 全 teardown；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- UMD 生成的真实 update 数组流量（`flag&2`，需 producer；r190 仍 open）。
- TQX 真发射立项；真实绘制执行（backend 接线）。
