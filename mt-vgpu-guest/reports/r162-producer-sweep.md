# r162：换 producer 探针——fill CCB 与源面数无关，copy 路径在 fabrication 下不可达

- **结论**：`-n 2` 的 fill 与基线 `(-device 0 -f -o)` 经机器比对除 `+0x40` 计数器（`770f` vs `8c0f`）外逐字节一致：fill CCB 不随源面数变化，扩展区条目算术无法用此旋钮闭合。copy 路径（去 `-f`）在 major 1/2 下均以同一位置 SIGABRT（`0x500d000` 映射后、`0x500e000` 分配前，`TQJobSubmit` 内 copy-setup 分发深处），无错误信息；该 producer 在当前 fabrication 下不可达。扩展区算术仍开放，待新的可复现 producer。本轮零硬件触碰、无代码改动。

## 实测

1. 同 shim/同环境三轮（`timeout -s KILL 15`；copy 两轮为 abort 退出）：
   - base `(-device 0 -f -o)`：528 行，Submit3 + `ccb_resolve`（39B/27 runs）。
   - n2 `(-device 0 -f -o -n 2)`：528 行，同 VA `0x8000f44000`、同 `0x1200`；机器比对非 run 欄位全等、27 runs 仅第 5 项不同（`64:770f`→`64:8c0f`），余 38B 一致。证据见 [`r162-n2-fill.jsonl`](r162-n2-fill.jsonl)。
   - nofill `(-device 0 -o)`：506 行（major 2）/505 行（major 1  legacy 复核同样 abort），止于 `mmap 0x500d000` 后，无 `0x89` 提交。证据见 [`r162-copy-abort.jsonl`](r162-copy-abort.jsonl)。
2. 离线 GDB 看 abort（major 2）：栈顶为 `TQJobSubmit → 无名静态函数 → abort`，无 stderr 信息。`TQJobSubmit+733` 处 `call 0x74889c0`（copy-setup 分发器：按 `0xa0(%r14)==0xb` / `%ah&2` / `%eax&0xa00` 分发，内调 `0x7488690` 等子处理）；该函数用 rsp-16 对齐序（`and $0xfffffffffffffff0,%rsp`）破坏帧链，故更深的 abort 叶只能看到两帧——继续单步归因性价比低，止于此。
3. 门禁：无代码改动（`/tmp` 内 `-g` shim 仅供 GDB，未入库），`make -C mt-vgpu-guest check-offline` 复核全绿（269 Python，1 skip + 272 C）；`lsmod` 无 `mt_*`。

## 边界

- “fill CCB 与源面数无关”只覆盖 `-n 1/2` 的离屏 fill；别的 fill 变体（颜色/尺寸）无命令行旋钮，未测。
- copy abort 的确切缺失前提未命名（疑某源面/格式对象在 fabrication 回包下为空）；不断言是 UMD 缺陷还是 shim fabrication 缺口。
- shim 回包仍 fabricated；不证明执行；不重载硬件 bridge。

## 下一步

- STATUS 步骤 1 的 CCB 侧已收敛到“fill 单一样本 + 计数器”；若无新 producer，转向步骤 2（update 数组活体语义，待可重建会话）或把 copy-setup 分发缺口单独立项（需先补源面创建链的 fabrication）。
