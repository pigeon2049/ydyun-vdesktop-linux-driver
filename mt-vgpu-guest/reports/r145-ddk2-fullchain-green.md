# r145：`0x2:0x8` 落地——DDK2 全链首绿（批准执行）

- **结论**：`0x2:0x8`（SyncFreeEvent，IN/OUT 各 4B，stub-ok）落地后，
  重建的新会话上 `b5*` 全链**全部返回 0、harness exit 0**：
  connect/devmemctx/render/syncprim/CCB-create/destroy 六符号全绿，
  122 行轨迹**零非零桥调用**（含 `0x82:0x12`、`0x2:0x2`×3、`0x88:0x5`、
  `0x88:0x6`、`0x2:0x8`，尺寸与实现一致）。
  DDK2 建销路径就此打通；r144 的 `b5*` 结论在新会话复验成立。
- 桥恢复默认 freeze（ref 1/0——新会话仅 pin，无历史累积），L3 全绿，零 WARNING。

## 实测（执行过）

1. 离线：dispatch 加 `case 0x8 → pvr_stub_ok`（SYNC 组，与 0x2:0x1/0x2:0x2/0x2:0x7
   同模型；表已有命名与 4/4 行，无需动表）；门禁 `test_sync_free_event_stubbed`
   （反向单行掐断即红×2——与 prim_set 共用返回行，属预期耦合，已还原并
   diff 核对）；`check-offline` 全绿；`make kernel` W=1 零警告（仅桥重编）。
2. 重建（r138 流程）：cold 0/1 全 idle → fresh-trial rc=0
  （trial `20261004T084935Z-5d5c38fb`，ref=1）；UMD 树内恢复（sha `b3058c02` ✓）。
3. 活体（`=2`，读回 2）：`b5*` 全链六符号全 0，exit 0。
   证据：`reports/r145-major2-fullchain.jsonl`（已入库）。
   插曲：首次误用 `u129`（本会话桥节点为 renderD128）致 connect 回 4 链中止——
   节点号必须每次从 `/dev/dri` 实测（Makefile 亦如此），已订正重跑。
4. 恢复：桥默认（0/0），node probe 0 failing，ref 1/0，dmesg 零 WARNING。

## 下一轮

- DDK2 kick 路径（`0x88:0x4` 在 DDK2 CCB 上的语义：accept-and-inspect 是否仍适用；
  真实绘制的非零 CCB 内容仍是 Translator T3 的缺失输入，r113 待新会话执行）。
- 遗留：80+ 提交未 push；`r135` jsonl 未动。
