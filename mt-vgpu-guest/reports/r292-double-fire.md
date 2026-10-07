# r292：同 translator 内连续两次 UMD fire（批准执行）——单发复位路径活体成立

- **结论**：第三窗口（脚本多轮化，`BLITS=2`）零干预全绿：两次真实 blit（各 137/8201 行）→ `fire seq=1: fired=1 chunks=21 verified=1` → **`fire seq=2: fired=1 chunks=21 verified=1`**（同 translator 寿命内，`fire_running` 复位 + 重定位 + 重验成立）→ 拆桥干净 → 默认桥 → L3 双绿 → 桌面拉回。CCB 第四/五样本 `nonzero=40`（轮值第 10/11 相异值 `fb 39`/`64 3a`）。窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：用户“继续真机推进”（动作类别同 r290/r291）。脚本增 `BLITS` 循环 + `WINID` 目录参数化 + fired 等待按 seq 过滤（`bash -n` 过；无内核改动）。
2. 发射（scope 逃生）：`desktop stopped` → `renderD128 free` → 三开 → round1（137/8201，fired seq=1）→ round2（137/8201，fired seq=2）→ `tri-open unloaded cleanly` → `default bridge back` → L3 双绿 → `window complete` → 桌面拉回；终态 refs 1/1。
3. 两轮 trace 均为单 tid、单 submit3@8201（seq 确定性第 5/6 次确认）；**零自污染**（unset 修连续两窗口生效）。
4. 证据：`r292-window-blit-{1,2}.jsonl`（0600）+ `r292-window.dmesg`（0600，38 行）；暂存区已清空。门禁状态沿用 r291（366+292，无内核改动）。

## 边界

- 单发复位只验了“跑完→再跑”；运行中并发第二发（`-EBUSY` 路径）仍只在源码门禁层（translator 并发冲突 r247 已在 translator 侧证实，全局 markers 不支持并发 submit——同构不同路径，不重复烧窗口）。
- CCB `nonzero=40` 连续四次（r290/r291/×2），+1 字节仍未定位；fire 不受影响。

## 下一步（候选）

1. TA/3D CCB 或 update 的 UMD 驱动验证（需先离线 recon producer/输入规约；blit 在 submit3 后 hanging 是挡在前面那座山）。
