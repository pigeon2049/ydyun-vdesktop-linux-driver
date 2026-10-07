# r244：legacy 真实 blit 在真桥下的行为（批准执行）——止于 `0x89:0x0` 拒绝

- **结论**：默认桥（legacy）上真实 `musa_blit_test -device 0 -f -o`（passthrough 记录）在 TransferContext create 即被拒：`0x89:0x0`（40/12）→ `-25`（`-ENOTTY`，桥无该 handler），UMD 随后 SIGABRT（134），**从未到达 `0x89:0x4`**。trace 8205 行（完整 legacy 建链序列，已入库）。refs 1/0 不变（拒绝无持有残留），L3 全绿，dmesg 干净。**会话未动（默认桥，未重载），freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0（默认桥在载）；`/tmp` 2%；dmesg 打 `[r244] legacy-blit-start` 标记。
2. 活体：blit 自枚举 `/dev/dri/renderD128`；`0x89:0x0` 1 次 `-25`，`0x89:0x5`/`0x89:0x6` 各正常（shmem 建销）；其余为建链调用。UMD 134 系其自身错误路径（r172/r174 同例）。
3. 事后：refs 不变；node probe 0 failing；dmesg 计数 0。

## 边界

- 与 r184 同形（`0x89:0x0 → -25` → SIGABRT），本轮增量是完整 trace 入库 + 新会话复核。
- legacy TDM 提交路径（`0x89:0x4`）仍无 handler——S4 边界，不在本轮。
- 未用 `timeout` 包裹；无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
