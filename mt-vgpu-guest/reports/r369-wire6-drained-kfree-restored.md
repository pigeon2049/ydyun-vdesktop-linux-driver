# r369: wire 6 已清除、kfree 回退上机，bridge 恢复 freeze 状态

**结论**：r368 诊断的恢复方案已完整执行——wire 6 经 `mt_drain_pending.ko` 安全清除（refcnt 1→0）、旧桥干净 rmmod、源码回退 r367 的 kfree 删除（消除 ~64B/次泄漏）后重建上机。新桥加载干净，dmesg 零 WARN/BUG/Oops，probe ref=1（未动）、bridge ref=0，freeze 恢复。

## 恢复步骤（逐项）

### 步 1：清 wire 6（`mt_drain_pending.ko`）
- 前置：基线确认 wire 6 仍 pending（dmesg `[26595.505338]` 起悬挂 ~1366s）、bridge refcnt=1、probe ref=1、无 oops；vermagic `6.12.111+deb13-amd64` 匹配。
- `insmod mt_drain_pending.ko enable=1` → dmesg：`dm[3] count=1` → `drained=1 remaining_total=0`。
- 安全性复核：wire 6 的 marker `m->job.state` 为 `MT_JOB_EMPTY`（跳过 job complete）、`m->context` 为 NULL（跳过解引用）、无 fence waiter；`dma_fence_set_error(-ETIMEDOUT)`+signal+put → `mt_marker_release` → `kfree_rcu` + `module_put` → refcnt 1→0。
- 确认 refcnt=0 后 `rmmod mt_drain_pending`，`lsmod` 无残留。
- 证据：`reports/r369-drain-log.txt`（0600）。

### 步 2：rmmod 桥
- 回滚件：`mt-vgpu-guest/build/traces/r369-recovery/mt_pvr_bridge.rollback-4a78b331.ko`（sha256 `1cc3d47f…`，即 r367 上机构建）。
- `rmmod mt_pvr_bridge` → "unloaded cleanly"；probe ref 仍为 1；dmesg 干净。

### 步 3：源码修正 + 重载（关键纠正）
- **修正内容**：回退 r367 的 kfree 删除——恢复 `pvr_cmd_musakickgfx2()` 成功路径的 `kfree(ctx)`，并替换错误注释。
  - 旧注释（r367，错误）："ctx ownership transferred to the pending marker (m->context)... Freeing here would be a use-after-free."
  - 新注释（r369，正确）："Marker-level: the op takes no context ownership (it clears work->context and never assigns m->context -- r368 falsified the r367 UAF claim). The dispatch-allocated ctx is therefore unreferenced after the op returns; freeing it here is a clean release, not a use-after-free."
  - 落点：`kernel/recovery/mt_pvr_bridge.c`（`pvr_cmd_musakickgfx2` 成功路径，`dma_fence_put(fence)` 之后）。
- **门禁**：`make -C mt-vgpu-guest check-offline` 411 Python + 299 C 全绿；`make kernel` W=1 零警告。
- **反向验证**（新测试 `tests/test_ta_kick_ctx_release.py`，3 tests）：删 success-path kfree → 1 failure（红）；恢复 → 3/3 绿。另断言 error path 亦有 kfree、r367 错误注释已删除。
- 按 r360 流程上机：`insmod` 新桥（sha256 `4f5b08af…`）→ kallsyms 见 `mt_bridge_submit_ta_work` 导出 → dmesg "registered 'pvr' node" → `/dev/dri/card1` + `renderD128` 正常。
- 回滚预案未触发（新桥一次加载成功）；回滚件保留于 `build/traces/r369-recovery/`。

### 步 4：健康确认
- dmesg：`grep -icE 'warn|bug:|oops'` → 0。
- refs：probe=1（全程未动）、bridge=0；bridge `holders/` 为空。
- `make probe` 未跑（WITH_BRIDGE 会 rmmod，同 r358/r360/r365–r368 取舍）；以干净加载 + 设备节点 + 导出符号 + 零 dmesg 异常为等效健康证据，如实记录。

## 文件
- 报告：本文件 `mt-vgpu-guest/reports/r369-wire6-drained-kfree-restored.md`
- 证据（0600）：`reports/r369-drain-log.txt`、`reports/r369-reload-log.txt`
- 源码：`kernel/recovery/mt_pvr_bridge.c`（kfree 回退）
- 测试：`tests/test_ta_kick_ctx_release.py`（3 tests，含反向验证）
- 回滚件：`mt-vgpu-guest/build/traces/r369-recovery/mt_pvr_bridge.rollback-4a78b331.ko`

## 门禁
- `make -C mt-vgpu-guest check-offline`：411 Python + 299 C 全绿
- `make kernel` W=1：零警告
- 反向验证：删 kfree → 红；恢复 → 绿（已实测）

## 诚实边界
- 本轮未验证 kfree 修复后的实际 dispatch（无 live TA kick）；功能验证留待后续轮次（生产 TA 完成路径，r368 建议的 r369+）。
- wire 6 的 firmware 侧状态未实测（drain 仅清理内核侧 pending marker）；若 firmware 曾发出 `0x100` 事件，其 DM3 队列毒化风险已随 `s->count` 归 0 解除（r368 分析）。
- `mt_drain_pending.ko` 为一次性恢复工具，未入库（gitignored，位于 `kernel/recovery/` 构建产物）。

## 下一步（另立轮次）
r368 建议：补齐**生产 TA 完成路径**（`mt_runtime_event` 对 TA marker 路由到 `mt_marker_complete_ta`），否则后续 TA marker 仍将永久悬挂——这是真正的 R2b 缺口。
