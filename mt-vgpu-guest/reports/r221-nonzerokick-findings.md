# r221：非零值 kick 活体ufacts——真 setter 是 `0x2:0xa` + translator 跳过 check 等待（批准执行）

- **结论**：本轮活体推翻两个离线假设（均以执行为准）：① UMD `SetSyncPrim` 导出实际发出的是 **`0x2:0xa`**（IN16/OUT4，桥 `-ENOTTY` → UMD 37），不是 `0x2:0x2`——objdump 地面实锤（`0x392db: mov $0xa,%edx`），Ghidra 伪 C 把 function id 写成 2；真身是跳板（`SetSyncPrim: jmp a0ad0`），r220 的 handler 挂错了位置（`0x2:0x2` 实为 free 路径清零器），下轮搬移。② check-only 翻译**根本不等 UFO 值**：value=1 vs PMR=0 一次通过（`translated kick: check=1 update=0 tag=1 fence=3`）——源码定位到 `if (nupdate) ret = pvr_translator_wait(conds, ncheck);`（r174 大提交引入，wait 的参数正是 check conds，门控却写 nupdate，极大概率笔误）。r212/r213 的“翻译”只证明了分发+marker+fence 机械，值匹配从未经验证（STATUS #2 口径不变，且更精确）。拆桥干净（probe 25→1），默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0（r220 构建在盘，strings/ vermagic 对版）；`/tmp` 2%；dmesg 打 `[r221] nonzero-kick-start` 标记。
2. `rmmod`（ref 0）→ `insmod translate_kick=1`（probe 未碰），节点仍 `renderD128`。
3. 链：r212 链 + `call SetSyncPrim 'b14*' '*b10' u1`（CreateSyncPrim 后）+ kick 值 1。`SetSyncPrim → 37`，trace 131 行中唯一 `0x2:0xa`（seq 129，IN16/OUT4/`ret -25`），`0x88:0x4`（seq 131）`ret 0`。证据：`reports/r221-nonzerokick.jsonl`（已入库）。
4. 语料核对（只按名查，不通读；SHA 已对版）：`SetSyncPrim = jmp a0ad0` 真身仅 0x2:0x2（`jbe` legacy）/0x2:0xd 两分支——与活体 0x2:0xa 矛盾；再挖 `0x392c0` 处 `mov $0xa,%edx` + `mov $0x2,%esi` + `mov $0x10,%r8d`，确认 wrapper 实际发（2, 0xa, 16B）。伪 C 不得直接当事实（§9），本次即例。
5. `*b10` 身份先以 fabricated `dump` 实测：buf10[0] 为 UMD 堆指针（syncobj*）——translator 在进程上下文跟随 UMD 对象解真 PMR（T2），故 UMD 封装路径自洽；raw 预置需真 PMR 柄（下轮）。
6. 恢复：`rmmod` → `unloaded cleanly`，probe 25→1；`insmod` 默认桥（盘内即 r220 构建）→ node probe 0 failing + smoke PASS；终态 1/0；dmesg `WARNING|BUG|Oops` 计数 0。

## 边界

- r220 的 `pvr_cmd_syncprim_set` 逻辑（定界/解析/写）不受影响，错的是挂载位置；下轮搬到 `0x2:0xa`，`0x2:0x2` 恢复 stub（含门禁改判）。
- `if (nupdate)` 改判需独立成轮（语义修复 + 门禁 + 双腿活体复验：匹配过、失配等 5s 超时）；本轮不动代码。
- 本轮未用 `timeout` 包裹 harness；UMD 行为（37 后继续走完链、exit 0）系其自身容错。

## 下一步（候选，需批准）

1. SyncPrimSet 搬移到 `0x2:0xa`（离线）+ 非零 kick 双腿复验（活体）。
2. `if (nupdate)`→`if (ncheck)` 修复（离线）+ check 值匹配/失配双腿复验（活体）。
