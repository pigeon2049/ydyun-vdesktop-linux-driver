# r224：observer 非零窗口活体验证（批准执行）——预置真数据，FNV 与离线预言逐字节一致

- **结论**：observer 非零路径在活体走通：`pvr_observe_ping` 在零窗口 fire 后，以 `0x2:0xa` 向同一窗口预置 5 个 u32（`0x11111111`…`0x55555555` @ off 0/4/8/12/16），再 fire 同 VA/size——桥报 `nonzero=20 first=0x0 fnv=0xb5e3eda7f6a52a47 head=11 11 11 11 … 55 55 55 55`，与本轮开工前离线算好的预言**逐项一致**（数/首/FNV/head 全中）。13 项全 ok，exit 0；refs 1/0 不变（默认桥，无需重载），dmesg 零 WARNING/BUG/Oops。**Freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0（在载即 r222 构建，observer + `0x2:0xa` 双在载）；`/tmp` 2%；FNV 预言离线算出（`fnv=0xb5e3eda7f6a52a47`）并记录在案；dmesg 打 `[r224] nonzero-observe-start` 标记。
2. 工具（离线部分）：ping 文件扩展非零相（preset 循环 + 第二 fire）；门禁 +1（preset 调用 + 第二 fire 回 0 期望）；`-Werror` 零警告构建。
3. 反向验证走弯路一次（注释掉 check 行仍通过——匹配串残留注释中，无效；如实记录），随后改错 function 号 → FAIL，还原 → OK，工具重编。
4. 活体：13/13 ok。零窗口行（nonzero=0）与非零行同现，相隔 14µs；事后 refs 不变；dmesg 仅两 observe 行。
5. `check-offline`：324 Python（323+1 新）OK；C/内核沿用 r222（本轮零改动，未重跑）。

## 边界

- 证明的是 observer 扫描/统计链 + SyncPrimSet 写链的联合正确性；窗口数据是手塑的，非 UMD 生成 CCB；执行语义未碰。
- 本轮 fresh file 自包含 + 全 teardown；默认桥配置；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- UMD 生成的真实 CCB 进 observer（fabricated GFX 重放 + r209 落盘链，或真实 3D producer）。
- TQX 真发射立项；真实绘制执行（backend 接线）。
