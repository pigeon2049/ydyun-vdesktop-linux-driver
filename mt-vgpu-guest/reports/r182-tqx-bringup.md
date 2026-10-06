# r182：TQX bring-up 打通（`-22` 定位到创建顺序）+ ref 记账部分澄清（批准执行）

- **结论**：`prepare failed: -22` 经分步日志定位到 `mt_execution_context_create`——TQX 块放在 process 创建之前（`!p->store`）。拆分为 Bo 绑定（seal 前）+ flavor-1 创建（process 后 DM 上下文旁）后，活体报 `submit3 tqx-ctx: ready`，bring-up 打通。ref 记账：translator 持有集 +18/rmmod 时 -18 对称归零（bring-up/teardown 对称性实证）；另有失败 prepare 轮次累计 +65（≈14/轮 ≈ map 数，未解释，功能无损）。门禁 282+292，反向全过；W=1 零警告。桥恢复默认 + L3 复绿，freeze 继续。

## 实测

1. 代码：`translate_tqx_ctx` 参数（默认 off）+ prepare 内 TQX 块 + teardown 扩展 + 分步日志；Bos 绑 seal 前、context 建于 process 后（r182 round-trip 修正）。
2. 活体（`=2` + 双 param，ref0 重载）：真实 blit → observe/dry-run（digest 与预言一致）→ `tqx-ctx: ready`。UMD 随后 hanging（无发射，预期内，timeout 终结）。
3. ref 差分：干净操作（probe/smoke/harness 链）Δ0；失败 prepare 的 blit 轮 +14（×3）/+23（×1，首轮）；成功轮 +18（translator 持有：10 DM Bo + 3 TQX Bo + 1 owner + 4 boot 借入）且随 rmmod -18（对称实证）。
4. 恢复：默认重载 + L3（node 0 failing，smoke PASS refs 平衡）+ dmesg 无模块 WARN；probe ref 回落中（见下）。

## 边界

- +65 未解释即动释放语义是大忌——下轮只做审计（defaults 下 legacy unwind 差分：有 map 无 prepare，干净分离）。
- bring-up 成功 ≠ 可发射：submit/fence/落位仍是 r183 的事；`translate_kick` 仍 off；无 GPU 提交。

## 下一步（候选，按序）

1. ref 审计（defaults legacy 差分 + instrument 备选）。
2. TQX 真发射（submit+fence+落位+像素回读）。
