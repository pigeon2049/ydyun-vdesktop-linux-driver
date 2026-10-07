# r231：多条目混合 kick 活体验证（批准执行）——2 check + 1 update 同轮

- **结论**：translator 多条件路径在活体走通：`pvr_update_writeback` 建两个 sync block，`0x2:0xa` 各预置一槽（V7/V8），混合 fire（check=2 + update=1）**44.8ms** 即时通过（与单 check 的 45ms 同构——双等待命中正常耗时）；update 写回 probe 0.11ms 即过。dmesg `check=2 update=1 tag=1 fence=9` → `check=1 update=0 tag=2 fence=10`（fence 序列延续）。probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r231] multi-check-start` 标记；`translate_kick=1` 重载（ref 0，probe 未碰）。
2. 工具（离线部分）：第二 sync block + 双 preset + `client_check_count=2` + 双 ufo 数组；门禁更新（count=2/VAL2/双 preset 断言）；`-Werror` 零警告构建。
3. 反向验证：count 改 1 → FAIL，还原（注意 Step2 probe 的 count=1 保留）→ OK，重编。
4. 活体：8 项全 ok，exit 0。事后 refs（bridge 0 / probe 25）；拆桥 `unloaded cleanly` 后 probe 25→1；默认桥 + node/smoke 全绿；终态 1/0；dmesg 计数 0。
5. `check-offline`：325 Python OK（总数不变：改断言不增项）；C/内核沿用 r222（本轮零内核改动，未重跑）。

## 边界

- 值是手塑的；证明的是多条件等待+写回链，不是 UMD 编组。
- 本轮 fresh file 自包含（两 PMR 随文件释放）+ 全 teardown；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）。
