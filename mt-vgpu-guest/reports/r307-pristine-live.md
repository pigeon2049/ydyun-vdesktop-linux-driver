# r307：pristine 归属定向首验——仍 FAIL（批准执行）

- **结论**：五开（含 pristine 规则）窗口：`dst: pristine override pool=0x1019 color=0xff0000ff`（归属按设计切到目的池）→ fire `todst=1` 全验 → bump → UMD 等待即过 → **仍 `Output does not match source` FAIL**。归属修对了方向（执行落到目的池是进步），但内容仍差：候选只剩 stride/颜色/比对区三项（r310 离线逐项判）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → 五开（含 r306 构建）→ 真实 blit（exit=1）→ 读 override/dst/fire/bump 行 → 拆桥 → 默认 → L3 双绿 → 拉桌面。
2. dmesg 执行值：`pristine override pool=0x1019` → `dst: pool=0x1019 ... forced=0` → `fired=1 ... todst=1` → bump → FAIL。VA 引用普查 `ccbref: total=10`（逐项归属见 `.dmesg`，本轮未及分析——r310 留用）。
3. 证据：`r307-pristine.jsonl`（0600，8290 行/178 调用，全序拆除）+ `r307-blit-stdout.txt`（0600）；暂存区已清空。
