# r291：UMD 驱动 fire 复现全绿（批准执行）+ r290 自重启订正

- **结论**：第二窗口（脚本直跑，无人工干预）完整全绿：停桌面 → ref 0 → 三开 → 真实 blit（137，8201 行）→ `fire seq=1: scheduled chunks=21` → `fired=1 chunks=21 verified=1 bad=0/1310720` → 拆桥干净 → 默认桥 → L3（node 0 failing/0 mismatch + smoke PASS）→ 桌面拉回。UMD 驱动 fire 跨两次加载可重复；CCB 第三样本 `nonzero=40`（轮值第 9 相异值 `dd 35`）。窗口零新增 WARN。**Freeze 已恢复。**
- **r290 订正（用户指正）**：两次“重启”均为用户手动重开桌面，无自重启证据；“90 秒定律”作废，约束实为用户容忍度。r290 报告相关两处已原地修正。

## 实测（执行过）

1. 批准：用户“是我手动开的，你继续真机推进”（桌面可再停 + 继续活体）。脚本时限收紧（blit 60s、fired 期限 60s；r290 实测 submit3 秒级到达、fire 1 秒）。
2. 发射（systemd scope 逃生，后台跑，前台轮询）：`desktop stopped` → `renderD128 free` → `node=renderD128` → blit 137/8201 行 → **fired 行一次命中**（LC_ALL 修后等待循环正常）→ `tri-open unloaded cleanly` → `default bridge back` → L3 双绿 → `window complete` → `desktop start issued` → 终态 refs 1/1（桌面重持为正常态）。
3. 本轮无人工干预、无 rmmod 被拒（上一轮的恢复曲折未重演；60s 预算远快于用户容忍）。
4. 证据：`r291-window-blit.jsonl`（0600，单 tid 79325、单 submit3@8201；**无自污染**——unset 修生效）+ `r291-window.dmesg`（0600，26 行）；暂存区已清空。无内核代码改动（桥 `.ko` 沿用 r290 构建）。

## 边界

- CCB 第三样本（同会话）：`va/4608/res/pmr/first` 全同；`nonzero=40` 连续两次（r290/r291），轮值字节第 9 相异值 `dd 35`，其后流 `01 02 20 10 03…` 与 r290 逐字节同。+1 字节仍未定位（全窗转储缺失），fire 不受影响。
- 脚本时限改动（60/60）与 r290 遗留修（LC_ALL/unset）随本轮提交；内核侧零改动。

## 下一步（候选）

1. TA/3D CCB 或 update 的 UMD 驱动验证（用户协调窗口；需先离线 recon producer/输入规约——blit 在 submit3 后 hanging，再往深走需要 fire 写真实目的池或 GDB 推进 UMD）。
