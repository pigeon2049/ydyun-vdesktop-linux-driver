# r337：零值在 BlitInit 入口已存在——缺失的生产者在上游（批准执行）

- **结论**：GDB 文件脚本单发真实 copy tq-perf（`=2` 窗口），四入口分阶段快照（寄存器 + `+0x58→+0x20→+0xc` 链），以指针值跨阶段关联：
  - `BLITINIT`：`rsi=0x…0240`（ctx），链 `[+58]=0x…1380 → [w20]=0x…08f0 → cnt=0`——**零值在链条第一眼已存在**；
  - `CHECKF`：参数全换（kick 结构 + 栈），不碰本链（与 r328 一致）；
  - `REL3320`：`rsi` 与 BLITINIT 同值（`0x…0240`），链逐字节一致，`cnt=0`——BlitInit→release 全程无人填（与 r336 写观察零命中互证）；
  - 堆地址跨三轮一致（`…0240/…1380/…08f0` 在 r334/r336/r337 同值），布局确定。
  缺失的生产者在 **BlitInit 上游**（`TQJobSubmit` 序言/`RGXQueueValidate` 一带，或 copy setup 根本没建表）——`+0x3320` 以内已无嫌疑人。`=2` 拆 → 默认回（major=0 已验）→ L3 双绿；refs 1/0，窗口零新增 WARN。**Freeze 已恢复。**无内核代码改动（窗口脚本 `scripts/stage-snapshot-window.sh` 落库）。
- **如实记**：`LOOKUPEOT` 行 `rdi=0x1` 系标量参数，其解引用值无意义（疑断中共享代码或标量入口，不承载结论，未用）。

## 实测（执行过）

1. 批准：用户“继续”（接 r336 既定下一刀）+ 真机/重载授权延续。预检 ref 0、无持有 → `drm_major=2` 重载 → GDB 单发（`timeout -s KILL 120`，即时 abort）→ 四快照 + abort 点 → 恢复默认 + L3。
2. 证据：`r337-stage-snap.txt`（0600：ARMED + 四行快照 + ABORTSITE）+ `r337-tqperf.jsonl`（0600，8701 行）；暂存区已清空。门禁沿用（394+299）。

## 边界与下一步

1. 下一刀：`TQJobSubmit` 入口快照（`nm -D` 取地址，同脚本再加一断）——若链在 JobSubmit 入口已是零，则表从未被建，copy producer 缺失定锤，转 RE/差分立项。
2. 本轮未停桌面（无持有即不停）；teardown 前复验 ref 0（未触发挡回）。
