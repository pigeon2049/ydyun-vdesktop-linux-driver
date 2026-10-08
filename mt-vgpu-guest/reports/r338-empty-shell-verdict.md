# r338：表是空壳——JobSubmit 入口链尚空，BlitInit 入口已建成零计数（批准执行）

- **结论（定锤）**：GDB 文件脚本单发真实 copy tq-perf（`=2` 窗口），`TQJobSubmit` 入口（`0x5ff00`）快照：`rdi=0x…b830`（堆 job），其 `+0x58` 链尚空（`[0]=[+58]=0`）；而 `BLITINIT` 入口 `rsi=0x…0240` ctx 链已全链接（`…1380→…08f0`）且 `cnt=0`，直至 `REL3320` 逐字节一致。ctx 空壳建于 **JobSubmit 入口与 BlitInit 入口之间**（序言 `RGXQueueValidate/RGXQueryTimer×2` 一带），计数恒零、无生产者填充——copy setup 的 surface 描述符表**从未被真正建造**。转立项条件达成：copy 路径缺 UMD 侧 producer（RE setup 全链或接受 copy 不可达，见 r317 déjà-vu）。`=2` 拆 → 默认回（major=0 已验）→ L3 双绿；refs 1/0，窗口零新增 WARN。**Freeze 已恢复。**无内核代码改动（窗口脚本 `scripts/jobsubmit-snapshot-window.sh` 落库，复用快照 helper）。
- **链条小结（r333–r338，一句话）**：骨架搭完（magic/`[r8]=1`）→ release 容量断言首轮死（`0>=0`）→ 表在 `+0x3320` 内无人写 → 零值 BlitInit 前已定 → 空壳建于 JobSubmit 序言。abort 是 UMD copy setup 的结构性缺失，与桥无关（101 调用全 0 依旧）。

## 实测（执行过）

1. 批准：用户“继续”（接 r337 既定下一刀）+ 真机/重载授权延续。预检 ref 0、无持有 → `drm_major=2` 重载 → GDB 单发（`timeout -s KILL 120`，即时 abort）→ JobSubmit/BlitInit/CheckFences/LookUpEOT/`+0x3320`/abort 六点 → 恢复默认 + L3。
2. 证据：`r338-jobsubmit-snap.txt`（0600）+ `r338-tqperf.jsonl`（0600，8729 行）；暂存区已清空。门禁沿用（394+299）。

## 边界与下一步

1. copy 线到此收敛为立项问题，不再烧单窗口：要么 RE setup 全链找建表缺口（深水），要么接受 copy 不可达、回填立项；TA/3D 同理需 producer recon。TA 与 copy 均非单窗口工程（r320 立场延续）。
2. 本轮未停桌面（无持有即不停）；teardown 前复验 ref 0（未触发挡回）。
