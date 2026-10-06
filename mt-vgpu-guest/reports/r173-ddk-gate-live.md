# r173：所谓“双峰”实为 DDK 门控——`=2` 走 Submit3，默认走 legacy 自拆（批准执行）

- **结论**：以文件 mtime 对 dmesg 重载线，所谓 submit/unwind 双峰与桥参数一一对应：`=2` 窗口（13:12–13:24）→ DDK2 `0x89:0x8` → `0x89:0xa`（`-25`）；默认窗口（13:24 后）→ legacy `0x89:0x0` → `-25` → 有序拆除（unmap/unreserve/release/disconnect，exit 正常）。r134 门控在活体逐字兑现；“同一桥行为不一”是跨重载边界取样所致，不存在 UMD 随机选路。活体桥 0x89 组仅实现 `{0x5,0x6,0x8,0x9}`，`0x0/0x4/0xa` 皆 `-ENOTTY`——两条路都按设计拒收。会话健康（probe 1/bridge 默认在载），freeze 继续。

## 实测

1. 对齐：4047 调用公共前缀后分叉（unwind 多一次 `0x1:0xa` AlignmentCheck——legacy 序列自带，非判决点）；致命分叉在 TransferContext 新建：`0x89:0x8`（DDK2）vs `0x89:0x0`（legacy）。
2. 终局：unwind 轮唯一桥非零是 `0x89:0x0 → -25`（seq 8180），随后 UMD 自行拆除，无崩溃；submit 轮终局 `0x89:0xa → -25`（另 `0xd:0x0 → -25` 未知组，记录）。证据：unwind 全 trace 存 [`r173-legacy-unwind.jsonl`](r173-legacy-unwind.jsonl)（8206 行）。
3. 桥分发表核对：`0x89:0x0/0x4/0xa` 无 case → `-ENOTTY`；`0x89:0x8` → `pvr_cmd_tdm_context2_create`。与两轮终局一致。
4. 更正 r172 留白：Rss=0/不可读发生于 `=2` 提交轮；默认轮无 Submit 可言，捕获必须在 `=2` 窗口做（下一轮）。

## 边界

- 门控输入（UMD 读到的 major 值在两窗口各是什么）未直接抓包——结论基于“窗口↔行为”完全相关（`=2` 窗 5+ 提交、默认窗 0 提交）与 r134 语料r141 活体；版本握手本身不在 trace 内（passthrough 不记 version ioctl）。
- r167 的 harness legacy-render NULL+8 与此无关（另一条路、另一崩溃点），仍开放。
- 无 GPU 工作；`translate_kick` 仍 off；两次重载已在 r172 记账，本轮零重载。

## 下一步（候选）

- 重开 `=2` 窗口做真实 CCB 字节捕获（提交时刻快照；Rss 之谜届时重验）；或实现 `0x89:0xa` accept-and-log（需重编+重载）。
