# r251：UMD standalone flake 现状（批准执行）——render 路径 10/10 崩，GDB 全过

- **结论**：UMD standalone render 路径当前零通过率：默认桥上 rung8 零值链 10 连试 10/10 SIGSEGV（`RGXCreateRenderContext` 处）；GDB 下同链**全过 exit normally**（r167 翻版，桥无罪）。flake 率演进：r72（~4/16）→ r167（11/11）→ r245（6/6）→ 本轮（10/10），疑与系统 uptime/内存状态相关（未归因，不展开）。UMD 链 soak 不可行（UMD 侧全崩）。refs 1/0 不变；node probe 首跑偶发 2 failing，重跑两次 0 failing（偶发未复现，如实记录）；dmesg 零 WARNING/BUG/Oops。**Freeze 继续。**

## 实测（执行过）

1. dmesg 打 `[r251] umd-soak-start` 标记；默认桥（R_V 后已恢复），未重载。
2. 10 连试：Makefile rung8 零值配方，10/10 exit 139，日志 `r251-soak-*.log`（未入库，纯崩溃无桥语义；如需复核 core 在 journal）。
3. GDB（用户态只读）：同链 `RGXKickSync → 0`、`exited normally`。
4. node probe：首跑 `FAIL: 2 failing step(s)`，随后两次 `OK: 0 failing`——偶发，重跑即过；refs 与 dmesg 全程干净。

## 边界

- 未验证 UMD 链 soak（UMD 侧不具备条件）；raw 工具 soak 由 r241 覆盖。
- 本轮教训：UMD 链活体 op 成功率随机器状态漂移，关键结论以 GDB 监督轮为准（r170 先例重申）。
- 未用 `timeout` 包裹；GDB 只读用户态；无代码改动。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
