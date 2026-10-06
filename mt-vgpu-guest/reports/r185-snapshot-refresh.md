# r185：快照刷新 pass（r165–r184 合并；零硬件触碰）

- **结论**：`PROGRESS-SNAPSHOT.md` 自 r164 后积压 20 轮，过期章节已同步：
  §1 补 r115–r184（DDK2 全链、check-only 活体、真实 39B CCB、T3 规约链、
  dry-run、TQX bring-up、ref 差分 Δ0）；§2 补 DDK2/SubmitTransfer3/
  dry-run/TQX 现状并重申 `-25`/`-ENOTTY` 系设计拒绝；§5 重写为
  r184 后四项（ref 收尾 → 真发射 → update → TA/CDM）；§6 门禁同步
  实测（282 Python 零 skip + 292 C；pmr 门禁 11→58；新增 submit3 行）；
  §11 前两项更新（CCB 归位、T3 缺口在输出侧）。§12 活页原样保留
  （历史轮次计数是同期记录，不改）。
- 本轮纯文档：`make check-offline` 基线未动（282+292，刷新前已验证）；
  所有新增引用路径逐项存在。
