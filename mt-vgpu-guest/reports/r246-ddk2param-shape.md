# r246：DDK2 `SetSyncPrim` param_1 三候选证伪（批准执行）—— harness 构造能力边界

- **结论**：r228 的后续 param_1 形状搜索完成，三候选全灭：`'b14*'`（sync prim context，r228）、`'b5*'`（devmemctx，r144 翻版假设）、`conn`（连接对象）——DDK2 下 `SetSyncPrim` 全部在 UMD 内 SIGSEGV（同 RVA `0xa0b38`，同 fault 形态 `0x8000000011`），`0x2:0xd` 从未发出。GDB 深挖 `b5*` 轮：`[param_1[0]]=0x555555569e00`（合法堆指针）→ `[+8]=0x0000008000000001`（桥 VA 常量，非指针）→ `[+0x10]` 野读；DDK2 分支要的是三级指针链（`param_1[0][1][2]`=handle），两种 context 的第二跳都是 VA 而非对象。语料确认 `SetSyncPrim` 是**外部导出**（calls.jsonl 无 UMD 内部调用者；`001a0ad0` 直调 wrapper）——合法输入（render context 或 device 上下文的 UMD 对象指针）超出 harness 当前构造能力。**工具边界，非桥缺口**（r228 结论维持并收敛）。拆桥干净，默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；UMD SHA `b3058c02…`；dmesg 打 `[r246] ddk2-b5star-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2`（probe 未碰），节点仍 `renderD128`。
3. `b5*` 轮：CreateSyncPrim 回 0，Set 处 SIGSEGV（exit 139）；trace 121 行，无 `0x2:0xd`。证据：`reports/r246-b5star.jsonl`（已入库）。
4. dmesg `segfault at 8000000011 ip ...[a0b38,...]` 与 r228 同形；GDB 寄存器对照（r12 随 param_1 变化，r14/rsi 不变）+ 内存转储定三级链断裂点。
5. `conn` 轮：同样 SIGSEGV（未留 trace，行为同形；不再为同一结论烧第三份 trace）。
6. 语料（只按名查）：`calls.jsonl` 中 `001782e0`（SetSyncPrim）`to:[]`——无内部调用者。
7. 恢复：`rmmod` → `unloaded cleanly`（probe ref 1 全程未动）；`insmod` 默认桥 → node 0 failing；终态 1/0；dmesg 计数 0。

## 边界

- 未证明 `0x2:0xd` 的 ABI（仍是语料假设）；未实现该 handler——无合法输入则实现不可验证（r228 口径延续）。
- DDK2 非零值 kick 链仍断；DDK2 零值链（r213/r234）不受影响。
- GDB 只读用户态；未用 `timeout` 包裹；无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- DDK2 UMD 对象暴露（harness 取 render context 对象指针的能力，离线先行）；TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）。
