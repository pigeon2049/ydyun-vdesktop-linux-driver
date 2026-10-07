# r222：SyncPrimSet 搬移到 `0x2:0xa` + `if (ncheck)` 修复 + 非零 kick 双腿活体全绿（批准执行）

- **结论**：r221 的两处证伪本轮闭环。离线：write handler 搬到 `0x2:0xa`（新宏 `MT_PVR_FN_SYNCPRIMCPUSIGNAL`，生成头同名同布局；`0x2:0x2` 恢复 stub）+ `if (nupdate)`→`if (ncheck)` 单行语义修复；门禁：syncprimset 改判 + translator 新增 wait 门控断言 + fn 56→57 + MAPPING 迁到 `(0x2,0xa)`（生成表 16/4 diff 通过）；双重复位验证（路由/语义各一）；`check-offline` 318+292 全绿；`make kernel` 零警告。活体（`translate_kick=1` 新构建）：Leg1 预置+匹配——`SetSyncPrim → 0`（此前 37），dmesg `syncprimset: sync=0x102d off=0 val=1`，0.045s 即时翻译 `fence=4`；Leg2 失配——5.007s 后 `RGXKickSync → 37`（等待真实存在，无 marker）；probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；盘内即 r220 构建；dmesg 打 `[r222] two-leg-start` 标记。
2. 代码（3 处）：wire.h 新宏 + 两处 struct 注释改判；bridge.c dispatch 搬移 + `if (ncheck)`；门禁 4 文件。
3. 反向：dispatch 改 stub + 语义改回 nupdate → 3 门禁红（syncprimset/render2/translator 各一）；还原即绿。
4. 重载：`rmmod`（ref 0）→ `insmod translate_kick=1`（probe 未碰），节点仍 `renderD128`。
5. Leg1（match）：r221 链 + `SetSyncPrim u1` + kick 值 1 → 全 0、exit 0、0.045s；trace seq 129 `0x2:0xa ret 0` + seq 131 `0x88:0x4 ret 0`。证据 `r222-leg1-match.jsonl`（131 行）。
6. Leg2（mismatch）：同链去预置 → 5.007s stall → kick 37，harness 存活退出；无新增 translated 行（marker 正确抑制）。证据 `r222-leg2-mismatch.jsonl`（130 行）。
7. 恢复：`rmmod` → `unloaded cleanly`，probe 25→1；`insmod` 默认桥（盘内即本轮构建）→ node 0 failing + smoke PASS；终态 1/0；dmesg 零 WARNING/BUG/Oops。

## 边界

- 值语义至此才是真闭环：r212/r213（机械）→ r221（证伪）→ r222（匹配过/失配等）。fence 序列 1→2→3→4 跨轮连续。
- UMD 的 `SetSyncPrim` 参数（`'b14*'` 上下文 + `'*b10'` 对象 + 值）由真机行为确认可用；Ghidra 伪 C 的 fn id 错误已用 objdump 钉死，教训记入 §8（见快照）。
- 本轮未用 `timeout` 包裹 harness（5s 等待由桥预算覆盖，r148 同例安全）；无 translator 之外的 live 模块。

## 下一步（候选，需批准）

- update 非零腿（`flag&2` 真实数组经 translator 写回语义，r159）：需 producer 或最小合法 update 块构造。
- TQX 真发射立项；真实绘制执行（backend 接线）。
