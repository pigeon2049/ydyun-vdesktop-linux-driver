# r165：复核——门禁/重放/证据链/文档一致性全量核对

- **结论**：`check-offline` 重跑全绿（269 Python，1 skip + 272 C）；blit 重放复现 r158/r160（同 VA/长度/reservation/PMR，39B/27 runs，runs 覆盖 39B）；UMD SHA（`b3058c02…`）、无模块在载均符合；r157–r164 报告与索引文件逐项存在。发现并订正 STATUS 一句话现状两处过期（CCB 归属、update 语义）；快照 `对应提交` 指针按 bA32 惯例落后一个 amend（`e9dd6a7`，仅 reflog 可达），属已知约定，不改。本轮零硬件触碰、无代码改动。

## 核对明细

1. 门禁：`make -C mt-vgpu-guest check-offline` → 269/272 全绿；shim `-Werror` 重编通过；`lsmod` 无 `mt_*`。
2. 重放：`musa_blit_test -device 0 -f -o`（major 2 + shared backing，`timeout -s KILL 15`）528 行，`ccb_resolve` 与 r158 关键欄位一致（`0x8000f44000`/`0x1200`/`0x900d`/`0x500e`/`0x500e000`），27 runs 恰好覆盖 39B——归属与窗口结论可重复。
3. 证据链：r157–r164 的 md/jsonl/txt 共 13 个文件逐项存在（上见复核命令输出）；索引 64 行；§12 活页到 r163（r164 为刷新 pass，无运行态变化，未立项，符合活页语义）。
4. 文档一致性：STATUS 门禁计数（269）与实测一致；快照 §5/§6 与 §12 一致；MEMORY 两节（r164/r163）与“最后更新”行一致。
5. 遗留记录：工作区 57 个未提交/未入库项均为历史包袱（本轮仅动 6 个文档）；`MEMORY-HISTORY-2026-10-05.md` 未入库，原样不动。

## 边界

- 复核只覆盖 L1 离线门禁与 fabricated 重放；L3/L4 与活体结论不在复核范围内（红线禁动）。
- 指针 trailing 惯例若改需另立项（涉及 amend 流程变更），本轮不动。
