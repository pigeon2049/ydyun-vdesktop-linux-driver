# r182：TQX bring-up 受阻——prepare 报 -22；另有 +23 ref 漂移待查（批准执行）

- **结论**：translator 进程下建 flavor-1 上下文 + 3 Bo（`0x40000000/0x40010000/0x40020000`，prepare 内 seal 前绑定）的首个活体窗口以 `prepare failed: -22` 告终（UMD 见非零中止，诚实大声失败，无 GPU 动作）。候选根因：flavor-1 路由/新绑定与 boot_shared 或 seal 排序冲突（boot VA 图未来得及读完即止损，未断言）。另发现 probe ref `1→24`（单 blit 轮内，14 map + 1 prepare 解释不了；无残留进程/fd，文件皆正常关闭，会话功能完好，仅禁 unload——unload 本就被 freeze 禁止）。bring-up 代码 param 门控（默认 off 原样）已入库，门禁结构覆盖；本轮不碰 DM 成品路径。桥恢复默认 + L3 复绿，freeze 继续。

## 实测

1. 代码：`translate_tqx_ctx` 参数（默认 off）+ prepare 内 TQX 块（seal 前）+ teardown 扩展；`make kernel` W=1 零警告；282+292 全绿（4 项 TQX 新门禁）；反向（翻默认）可抓。
2. 活体（`=2` + 双 param，ref0 重载）：真实 blit → observe/dry-run 正常 → tqx hook → `prepare failed: -22` → 桥回错 → UMD SIGABRT（预期内）。
3. ref：开工 1，单轮后 24；`lsmod` Used by 同步 24；L3/smoke 后不变（干净操作不新增）；`Guest/FW` 会话功能未见异常（L3 全绿为证）。
4. 恢复：默认重载 + L3（node 0 failing，smoke PASS refs 平衡）+ dmesg 无模块 WARN。

## 边界

- `-22` 具体行未定位（缺分步日志，下轮加）；TQX 上下文/Bo 语义本身未经验证，不能排除“此路本就不通”（flavor-1 需独立进程/空间？）。
- +23 未解释即改动任何释放路径是大忌——下轮只做审计（对照 `try_module_get`/`module_put` 全表 + refcnt 差分实验），不动释放语义。
- 无 GPU 提交；`translate_kick` 仍 off。

## 下一步（候选，按序）

1. `-22` 定位：prepare 内分步日志（重编+重载一轮）或离线读完 boot_shared VA 图二选一。
2. ref 审计：干净操作差分（smoke/blit/prepare 各自前后 refcnt），定位泄漏点。

## 后续验证（同轮，r182-tqx-bringup.md 为准）

- `-22` 已定位：TQX 块原在 process 创建之前（`!p->store`）；拆分为 Bo 绑定（seal 前）+ flavor-1 创建（process 后 DM 上下文旁）后，活体报 `tqx-ctx: ready`，bring-up 打通（无 GPU 动作）。上文“此路本就不通”之忧解除——flavor-1 可与 DM 进程共存。
- ref 记账更新：translator 持有集 +18 随 rmmod -18 对称归零（bring-up/teardown 对称实证）；失败 prepare 轮次另累计 +65（≈14/轮 ≈ map 数，未解释；无残留/fd，功能完好，仅禁 unload）。完整证据与差分数据见 `r182-tqx-bringup.md`。
