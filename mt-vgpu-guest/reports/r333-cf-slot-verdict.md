# r333：CheckFences 出参判决书——`[r8]=1` 在 abort 时仍成立（批准执行）

- **结论**：GDB 文件脚本（catch-load + python 现算绝对地址，r322 模式）单发真实 copy tq-perf（`=2` 窗口）：`TQ_CheckFences` 仅命中一次（type=0/count=1/flags=0/a8=0，与 r328 两轮一致，且 `[rsi+0x204]=1` 反证 `rsi=ctx`）；`r8=0x7fffffffc344`（调用者栈局部）；abort 调用点（file `0x2c8cc`）处该槽内容仍为 **1**。CheckFences 写了 1（有 fence 要等或已满足），abort 仍在其下游发生——“mismatch 标志”解读彻底死亡，abort 条件在 LookUpEOT 之后的状态累积中。`=2` 拆 → 默认回 → L3 双绿；refs 1/0，窗口零新增 WARN。**Freeze 已恢复。**无内核代码改动（新增窗口脚本见下）。
- **副产品**：`scripts/cf-slot-window.sh` 落库（r290 `fire-window.sh` 同构：trap 兜底恢复、trace 落 `build/traces/r333/`、桌面不停——本轮 renderD128 全程无持有）。首跑空跑：python 里 `printf "…%p…", cf` 把 python 变量当 inferior 符号（`No symbol "cf"`），改字符串拼接后命中。备忘：GDB `printf` 的参只能是 inferior 表达式。

## 实测（执行过）

1. 批准：用户“允许重建 render”。预检 ref 0、无持有 → rmmod 默认桥 → `drm_major=2` 重载 → GDB 单发（`timeout -s KILL 120`，即时 abort）→ 读槽值 → rmmod → 默认回（`drm_major=0` 已验）→ L3 双绿。
2. 证据：`r333-cf-slot.txt`（0600：ARMED/CF/ABORT 三行 + 槽转储）+ `r333-tqperf.jsonl`（0600，8633 行）；暂存区已清空。门禁沿用（394+299）。
3. 缺口：abort 点 `bt 8` 无输出（原因未查——r317 core 已有 `TQJobSubmit` 栈，不展开）。

## 边界与下一步

1. 下一刀：LookUpEOT 之后、abort 之前的状态——`RGXReleaseCPUMappingZSBuffer+0x3320` 返回值（r319 候选），或 job 结构差分（r322 候选）；另行批准。
2. 本轮未停桌面（无持有即不停，减少扰动）；teardown rmmod 前复验 ref 0， blocked 则停手（未触发）。
