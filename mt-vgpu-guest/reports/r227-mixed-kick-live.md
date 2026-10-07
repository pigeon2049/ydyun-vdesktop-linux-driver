# r227：混合 kick 活体验证（批准执行）——check 等待 + update 写回同轮闭环

- **结论**：translator 完整语义（r159）在活体闭环：`pvr_update_writeback` 升级为三相——Phase 0 以 `0x2:0xa` 预置 check 槽=V7；Phase 1 混合 fire（check{off0,V7} + update{off4,V9}）**44.8ms** 即时通过（与 r222 Leg1 的 45ms 同构——预置命中的正常耗时，远小于 5s 预算）；Phase 2 check probe{off4,V9} **0.11ms** 即时通过。dmesg 双行 `check=1 update=1 tag=1 fence=7` → `check=1 update=0 tag=2 fence=8`（fence 序列延续）。probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r227] mixed-kick-start` 标记。关键判断：混合 fire 必须走 translator，故 `translate_kick=1` 重载（ref 0，probe 未碰）。
2. 工具（离线部分）：Phase 0 预置 + Phase 1 混合接线（check/update 双数组同 kick）+ Phase 2 改探 update 槽；门禁更新（mixed 接线/回 0 期望，旧 update-only 断言退役）；`-Werror` 零警告构建。文件头注释改用脚本替换（edit 多行匹配失败一次，如实记录）。
3. 反向验证：改错混合标签大小写 → FAIL，还原 → OK，重编。
4. 活体：7 项全 ok，exit 0。事后 refs（bridge 0 / probe 25）；拆桥 `unloaded cleanly` 后 probe 25→1；默认桥 + node/smoke 全绿；终态 1/0；dmesg 计数 0。
5. `check-offline`：325 Python OK（总数不变：删一加一）；C/内核沿用 r222（本轮零改动，未重跑）。

## 边界

- 值是手塑的（V7/V9），不是 UMD 生成的 `flag&2` 数组；证明的是 translator 混合链（等→标→写），不是 UMD 编组。
- 本轮 fresh file 自包含 + 全 teardown；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- TQX 真发射立项（离线实现先行）；CCB 内容解读（离线）；真实绘制执行（backend 接线，离线大工程）。
