# r233：多 update 条目活体验证（批准执行）——2 update 全写回

- **结论**：translator update 循环多条目在活体走通：`pvr_update_writeback` 的 update 数组扩为 2 条目（off4/V9 + off8/V10，同 PMR），混合 fire（check=2 + update=2）**45.7ms** 即时通过；Step2 改探**第二槽**（off8/V10）0.10ms 即过——证明 update 循环发布每一个条目，不止首项。dmesg `check=2 update=2 tag=1 fence=13` → `check=1 update=0 tag=2 fence=14`（fence 序列延续）。probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r233] multi-update-start` 标记；`translate_kick=1` 重载（ref 0，probe 未碰）。
2. 工具（离线部分）：update 数组 2 条目 + Step2 改探第二槽 + `UPDATE_VAL2`；门禁更新（update count=2 断言）；`-Werror` 零警告构建。
3. 反向验证：count 改 1 → 双门禁 FAIL（update 接线 + mixed 接线），还原 → OK，重编。
4. 活体：8 项全 ok，exit 0。事后 refs（bridge 0 / probe 25）；拆桥 `unloaded cleanly` 后 probe 25→1；默认桥 + node/smoke 全绿；终态 1/0；dmesg 计数 0。
5. `check-offline`：325 Python OK（总数不变：改断言不增项）；C/内核沿用 r222（本轮零内核改动，未重跑）。

## 边界

- 值是手塑的；证明的是 update 循环全发布，不是 UMD 编组。
- 本轮 fresh file 自包含 + 全 teardown；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）。
