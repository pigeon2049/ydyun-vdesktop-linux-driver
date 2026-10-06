# MEMORY HISTORY — 2026-10-06

> 由 `MEMORY.md` 清理周期移入（只保留最新两节），原样保留。

## 本次会话进展（r181：transfer dry-run 活体验证）

- 真机已批准（含内核改动+三次 `=2` 重载，均 ref0，probe 零触碰）。`translate_transfer` dry-run：首轮选择 bug（CCB PMR 胜出→错配大声失败）修复后，次轮程序 digest 与离线预言逐位一致（`0xd893...`）；附带修 fill 未初始化（双门禁）。278+292 全绿，反向全过。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r181-dryrun-verified.md`。候选下一步：真发射（scratch+fill+fence+回读）。

---

## 本次会话进展（r182：TQX bring-up 打通）

- 真机已批准（含内核改动+多次 `=2` 重载，均 ref0 操作，probe 会话零触碰语义）。初版 `-22` 经分步日志定位为 TQX 块在 process 创建之前（`!p->store`）；拆分为 Bo 绑定（seal 前）+ flavor-1 创建（process 后 DM 上下文旁）后，活体报 `tqx-ctx: ready`，bring-up 打通（无 GPU 动作）。
- ref 记账：translator 持有集 +18 随 rmmod -18 对称归零（对称性实证）；失败 prepare 轮次另累计 +65（≈14/轮 ≈ map 数，未解释；无残留/fd，功能完好，仅禁 unload——unload 本就被 freeze 禁止）。代码 param 门控已入库（282+292，反向全过）。桥恢复默认 + L3 复绿，freeze 继续。
- 证据：`reports/r182-tqx-bringup.md`。候选下一步：ref 审计（defaults legacy 差分）+ 真发射。

---

## 本次会话进展（r183：ref 漂移离线审计）

- 零硬件触碰（只读代码审计 + `lsmod` 只读；未重载模块、未提交 GPU 工作；`dmesg` 本容器无权读）。首要嫌疑已命名：`pvr_file_release` 双 early-return（unbind/destroy 失败即 `return`，`mt_pvr_bridge.c:868-875`）可 abandon 整文件 PMR 的 `dma_owner`（每 map +1），量级 ≈14/轮与失败轮 +14 同形；活体 probe Used by=66（=1+65）只读吻合。prepare 失败路/DMA 注册释放经走查配平，已排除。以上为推断，活体差分待可重载窗口（需批准）；释放语义未动。
- 证据：`reports/r183-ref-audit-offline.md`。候选下一步：活体差分（defaults legacy 差分，需批准）→ 真发射。

---

## 本次会话进展（r184：defaults 活体差分 Δ0）

- 批准执行单次活体（无重载、无 GPU 工作：legacy `0x89:0x0 → -25` 后 SIGABRT）。真实 blit（passthrough 记录，UMD SHA `b3058c02…`）：9 maps/11 mmaps/abort/close 后 probe 66→66、bridge 1→1（Δ0），无 D 态。maps 无罪；+65 与 prepare/挂起强相关——r183 假设修正为条件触发（kill-while-busy 命中 destroy `-EBUSY` 才 abandon；干净 abort 不触发）。
- 证据：`reports/r184-defaults-differential-d0.md` + `r184-defaults-blit.jsonl`。候选下一步：kill-while-busy 关账轮（待批）→ 真发射。

---

## 本次会话进展（r185：快照刷新 pass）

- 零硬件触碰。r164 后积压 20 轮，快照 §1/§2/§5/§6/§11 已同步 r165–r184
 （门禁实测 282 + 292；pmr 门禁 11→58；§5 重写为 ref 收尾 → 真发射 → update → TA/CDM）。§12 历史计数为同期记录，不动。
- 证据：`reports/r185-snapshot-refresh.md`。候选下一步：等硬件批准（kill-while-busy 关账 / 真发射 / `=2` update 验证）。

---

## 本次会话进展（r186：scene 预设值抽取）

- 零硬件触碰。bridge 与 6 live 文件重复写死的 scene VA 收敛到新建
  `kernel/mt_addr_plan.h`（15 宏）；stream 端点等异语义字面量保留。
  过程撞车 `MT_TQX_STATE_BYTES`（`mt_tqx_copy.h` 同名 `0xa8`）被 `W=1`
  抓获，改名解决——此前碰撞检查被 `head -3` 截断，教训重演。
  新增门禁 5 项 + 反向验证；全量 287+292 全绿，`W=1` 零警告。
- 证据：`reports/r186-addr-plan.md`。候选下一步：等硬件批准（关账 / 真发射 / `=2` update）。

---

## 本次会话进展（r187：预设复核 + 审计重构）

- 零硬件触碰。第二轮扫面 bridge 全部裸字面量/WARN/锁点/分支：
  可动作两项已重构（PCI 槽位宏统一、`0x88` 功能号命名进 wire.h）；
  其余 6 类故意保留（注释/断言在位）。新增门禁 3 项 + 反向验证；
  全量 290+292 全绿，`W=1` 零警告。
- 证据：`reports/r187-preset-audit-refactor.md`。候选下一步：等硬件批准（关账 / 真发射 / `=2` update）。

---

## 本次会话进展（r188：保留项全抽取）

- 零硬件触碰。55 dispatch 标签命名进 wire.h（逐组计数替换，零残留）；
  PMR 三尺寸进 bridge 顶部；stream/slot 三宏进 addr_plan（含 readback
  除数）。未分发 ID 不命名。15 个旧门禁同步到宏形式；两处误伤已纠正。
  新增 fn_ids 门禁 2 项 + 反向验证；全量 292+292 全绿，`W=1` 零警告。
- 证据：`reports/r188-fn-table.md`。候选下一步：等硬件批准（关账 / 真发射 / `=2` update）。

---

## 本次会话进展（r189：对象查找去重）

- 零硬件触碰。`pvr_object_find` 收敛 9 处重复查找；map 删锁内重复
  reservation 查找；另走查 connect/event/info/heap/pmr/open 等区域，
  结论均为不动。新增门禁 3 项 + 反向验证；全量 295+292 全绿，
  `W=1` 零警告。
- 证据：`reports/r189-object-find.md`。候选下一步：等硬件批准（关账 / 真发射 / `=2` update）。

---

## 本次会话进展（r190：update 路径定位）

- 零硬件触碰。语料按名定位：非零 update 走 TA 链
  `RGXKickGfx→SubmissionSetUpdateSyncPrim→BridgeRGXKickTA3D5`
  （0x82:0x14，IN 108/OUT 4）；桥无此 handler，requirements 表亦
  `CMD_LAST`——即 STATUS 第二项的 concrete 缺口。活体计划已列
  （observer + producer 待定）；producer 本身仍 open。
- 证据：`reports/r190-update-path-recon.md`。候选下一步：producer recon（离线）或等硬件批准。

---

## 本次会话进展（r191：kill-while-busy 关账轮）

- 批准执行活体（无重载：`rmmod` 被会话工具链持有挡回 EBUSY）。
  GDB 监督 #104 mmap 处击杀（死时 3 MAPs live），事后 66→66（Δ0），
  无 D 态，L3 复绿。file_release 假设至此无活体支持；+65 仍未命名，
  边界收紧（maps/abort/击杀/prepare 记账/残留进程全排除）。
  7 活体轮零新增泄漏。
- 证据：`reports/r191-killbusy-d0.md` + `r191-killbusy.jsonl`。候选下一步：解持有后 `=2` 轮 / 真发射。

---
