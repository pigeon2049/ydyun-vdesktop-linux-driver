# r285：fire 模块 soak 重复性（批准执行）——5 轮装/打/卸全绿

- **结论**：同模块 5 轮 `insmod enable=1` → `run=1` → `rmmod`：轮轮 `fired=Y verified=Y chunks=21 result=0`（`.dmesg` 计 5 行 fired=1 + 5 行 prepared），轮轮 probe ref 1→1、bridge ref 1→1，节点每轮出现即消失。本轮窗口零新增 WARN。**bridge 未碰，freeze 继续。**无代码改动。

## 实测（执行过）

1. 批准依据：用户本轮指令“继续真机推进”（动作类别同 r283/r284）。预检：仅 bridge+probe 在载，节点仅 card0/card1/renderD128。
2. 5 轮循环（默认矩形 1280×1024/`0xff0000ff`）：每轮装载 rc=0、run rc=0、卸载 rc=0；`--` 输出见下（`run_rc=0 fired=Y verified=Y chunks=21 result=0 refs 1->1` ×5）。单发语义下每轮都是新加载，无 `-EBUSY` 残留。
3. 窗口 WARNING/BUG/Oops 计数 0；终态 refs 1/1，`/dev/dri` 回 card0/card1/renderD128。
4. 证据 `reports/r285-soak-live.dmesg`（0600，26 行）；暂存区已清空。门禁状态沿用 r283（366+292，无代码改动）。

## 边界

- soak 只覆盖默认矩形；大矩形重复性、跨参数交替未试（单发 + 重载机制同构，风险低）。
- bring-up/teardown 5 轮对称，无 r263 类锁残留迹象（每轮 `rmmod` 即时成功，无 D 态）。

## 下一步（候选）

1. 合并策略 recon（离线）：fire 合进 bridge vs 保持独立模块（现状 bridge 不可重载，合入也无上机路径——结论可能倾向保持独立 + 收敛为正式工具）。
