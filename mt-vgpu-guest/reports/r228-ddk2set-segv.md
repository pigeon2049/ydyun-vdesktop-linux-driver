# r228：DDK2 下 `SetSyncPrim` 用户态崩溃定位（批准执行）——缺合法 param_1 形状，非桥缺口

- **结论**：DDK2 侦察轮证实：`=2` 桥上 `call SetSyncPrim 'b14*' '*b10' u1` 在 UMD 内 SIGSEGV（exit 139），trace 停在 92 条 bridge 调用（末为 seq 121 `0x2:0x7`），`0x2:0xd` 从未发出。dmesg + GDB 双定崩溃 RVA `0xa0b38`（`mov 0x10(%rax),%rsi`，fault `0x8000000011`）：DDK2 分支把 param_1（sync prim context）当 device 上下文解 `[0]→[+8]→[+0x10]` 取 bridge handle，而 harness 传的 connect 派生 context 该槽是桥 VA 常量 → 野读。legacy 分支走 syncobj 侧（`puVar1[1]`），故同一调用在 legacy 下成功（r222 Leg1）。**这不是桥的缺口**：`0x2:0xd` handler 的需求仍在，但合法 param_1 形状是 open 问题（与 r190 的 TA producer 缺口同类）。拆桥干净，默认恢复 + L3 全绿，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；UMD SHA `b3058c02…`；dmesg 打 `[r228] ddk2-set-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2`（probe 未碰），节点仍 `renderD128`。
3. DDK2 全链（`b5*`，r213 配方）+ `SetSyncPrim`：CreateSyncPrim 回 0，Set 处 SIGSEGV。证据：`reports/r228-ddk2set-segv.jsonl`（92 调用，无 `0x2:0xd`）。
4. GDB（用户态调试，不碰内核）：崩溃 PC 落 UMD RVA `0xa0b38`；dmesg `segfault at 8000000011 ip ...[a0b38,...]` 互证。反汇编入口链：`r14=syncobj+0x18`，`r12=param_1`，`GetFeatures+0x54>1` → DDK2 分支 → `rax=(r12)[0]` → `rax=rax[1]` → `rsi=rax[2]` 崩。fault 地址形态（桥 VA 常量 + 偏移）证明 `[0]` 槽不是结构体指针。
5. 恢复：`rmmod` → `unloaded cleanly`（probe ref 1，全程未动）；`insmod` 默认桥 → node 0 failing + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 未证明 `0x2:0xd` 的 ABI（wrapper `FUN_00139360` 的 IN 28/OUT 4 仍是语料假设）；未实现该 handler——无合法输入形状，实现即不可验证。
- DDK2 非零值 kick 链仍断在值预置这一步；DDK2 零值链（r213）不受影响（新鲜 PMR 读零即过）。
- 本轮未用 `timeout` 包裹 harness（崩溃即时，无 hanging）；GDB 只读用户态。
- 无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- DDK2 param_1 合法形状 recon（离线反汇编：DDK2 下谁构造带 device 上下文的 sync context）。
- TQX 真发射立项（离线先行）；CCB 内容解读（离线）；真实绘制执行（backend 接线，离线大工程）。
