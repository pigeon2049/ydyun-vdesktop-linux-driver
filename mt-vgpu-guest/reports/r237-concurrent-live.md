# r237：多文件并发验证（批准执行）——双 ping 并行，per-file 隔离成立

- **结论**：桥 per-file 隔离在活体证实：默认桥上两个 `pvr_observe_ping` 进程并行（绝对路径 + CCB 绝对路径），**双双 exit 0 全 PASS**；dmesg 6 行齐（2 进程 × 3 行），各自独立 res/pmr 句柄（`0x1002/0x1001` 与 `0x1004/0x1003`），同 VA 零串扰。事后 refs 1/0，L3（node + smoke）全绿，dmesg 干净。**Freeze 继续。**

## 实测（执行过）

1. dmesg 打 `[r237] concurrent-start` 标记；默认桥（R_G 后已恢复），无需重载。
2. 首跑走弯路两次（如实记录）：① 相对路径后台任务 cwd 异常（127）；② 工作目录不对致 CCB 相对路径打开失败（双 FAIL）。改绝对路径后双 PASS——工具 CCB 路径约束（r225 已声明）重申。
3. 恢复：无需重载；L3 全绿；终态 1/0；dmesg 计数 0。

## 边界

- 只证明 observer 路径的 file 隔离；translator 多文件并发未验证（各持 translator 全局锁，串行化是设计行为）。
- 无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
