# r394：live 前安全测试落地——三类事故各有门禁拦截（离线）

> **结论**：用户要求"调试前增加更多测试，避免弱智错误导致冷重启"。本轮为今日三次冷重启事故各写了复盘，并落地三类离线安全测试（T1 VM 完整性 / T2 opcode 白名单 / T3 卸载安全）+ `scripts/safe_rmmod.sh` + pre-live 检查清单。三类测试反向验证全部通过（故意破坏→测试失败→还原）。门禁 472+299 全绿。

## 1. 事故复盘：本应捕获的断言

| # | 事故 | 根因 [实测] | 本应捕获的测试断言 |
|---|---|---|---|
| 1 | r375 oops → 冷重启 | bridge 手动拼装 `struct mt_gpu_vm`，遗漏 `ranges`/`page_lists`（`mt_gpu_vm_bind_many+0x300` oops，D-state，bridge 无法卸载） | **T1**：源码扫描——`->ranges=`/`->page_lists=` 等 VM 内部字段赋值禁止出现在 `mt_gpu_vm.h` 之外；所有 VM 创建必须经 `mt_gpu_vm_init()` |
| 2 | r380 trial 被清除 → 冷重启 | DM2（3D 队列）上提交了未验证的 opcode `0x66`（TA opcode）；firmware 忽略包**并清除 trial**（`0x890` 2→0） | **T2**：`(dm, opcode)` 白名单——`(2,0x66)` 永禁；TA opcode 钉死 DM3、3D opcode 钉死 DM2；pre-live 清单要求先声明 `(dm,opcode)` 并查表，未知组合先开离线研究轮（如 r381） |
| 3 | `rmmod -f` → 内核挂起 → 冷重启 | probe 被 trial pinning（ref=1）且 firmware 侧会话已清除，状态不一致；`rmmod -f` 强卸导致挂起 | **T3**：仓库级 `rmmod -f`/`--force` 黑名单；`scripts/safe_rmmod.sh` 查 `lsmod` refcount，非 0 拒绝并报错；refcount 异常 → 停轮报告，不强卸 |

## 2. 实现落点

### T1：`mt-vgpu-guest/tests/test_vm_init_integrity.py`（2 tests）
- 扫描 `kernel/**/*.c|h`（除 `mt_gpu_vm.h` 自身）：禁止 `->ranges=` / `->page_lists=` / `->bindings=` / `->binding_capacity=` / `->max_ranges=`（`.`/`->` 两种写法），禁止 `struct mt_gpu_vm x = {...非零...}` 指定初始化（`={0}`+`mt_gpu_vm_init()` 的 selftest 模式放行）。
- 自检：`mt_gpu_vm.h` 确实拥有这些字段赋值（防 `FORBIDDEN_FIELDS` 过期）。
- 现状：零违规（r376 后已无手动拼装）。

### T2：`mt-vgpu-guest/tests/test_opcode_whitelist.py`（4 tests）
- `PROVEN` 表（`(dm,opcode)→出处`）：`(0,0x46/0x47)` trial（每 boot 活体）、`(1,0x67)` TQX（r37–r41）、`(1,100)` TQX null marker（`mt_live_marker` 默认）、`(2,0x68)` 3D（r381）、`(3,0x64/0x66)` TA（r365）。
  - 说明：轮次简报列的是 `{(3,0x66),(3,0x64),(2,0x68)}`；TQX 与 trial 系活体已证，故纳入并注明出处，未知组合仍须先研究。
- `FORBIDDEN = {(2,0x66): "r380"}`，永禁。
- 测试：TA opcode（`MT_FW_TA_OPCODE`=0x66）仅 `mt_marker_fence.h` 引用、且 `mt_marker_submit_ta_work` 以 `const u32 dm = MT_FW_DM_TA`（=3）提交，`mt_fw_ta_marker_command` 仅在 TA 提交路径内调用；3D 同理钉死 DM2，且 3D 路径不得出现 TA opcode 符号；trial opcode 在 DM0。
- 现状：全绿。

### T3：`mt-vgpu-guest/tests/test_pre_live_safety.py`（3 tests）+ `mt-vgpu-guest/scripts/safe_rmmod.sh`
- 仓库级扫描（`.py/.sh/.md/.c/.h/Makefile` 等，跳过 `.git/decompiled/build/downloads`）：`rmmod -f`/`--force` 零容忍（`test_pre_live_safety.py` 与 `safe_rmmod.sh` 自身因"记录禁令"被显式 ALLOWLIST）。
- `safe_rmmod.sh`：`lsmod` 查 refcount；未加载→0 退出；持有者为名→exit 3 拒绝；数字非 0→exit 4 拒绝；仅 0 时 `sudo -n rmmod`。用法：`mt-vgpu-guest/scripts/safe_rmmod.sh mt_pvr_bridge`。
- 完整性：T1/T2 文件存在性断言（pre-live 门禁齐套）。
- 现状：仓库零 `rmmod -f`（历史事故为即席命令行，未入库）。

### Pre-live 检查清单（人读，见 `test_pre_live_safety.py` docstring）
1. `(dm, opcode)` 是否在 `PROVEN` 表中？否 → 先开离线研究轮，NEVER 上机试探。
2. 本轮是否创建 `struct mt_gpu_vm`？是 → 确认走 `mt_gpu_vm_init()`（一次性探针模块同样适用）。
3. 卸载用 `safe_rmmod.sh`；NEVER `rmmod -f`；refcount≠0 → 先 teardown 持有者，不明 → 停轮报告。
4. trial 前置：`0x890`/firmware 状态符合预期？mismatch → 停轮（r380 的 test-2 前置检查避免了第二次事故）。
5. 一次一个 live 模块；`timeout` 不进临界区（既有红线）。

### AGENTS.md 集成
- §5 红线新增：NEVER `rmmod -f`/`--force`（用 `mt-vgpu-guest/scripts/safe_rmmod.sh`）。
- §6 门禁新增：live 轮次开工前 MUST 跑 `tests/test_pre_live_safety.py`（r394 pre-live 门禁）。
- §8 检查单新增：`(dm,opcode)` 是否已在白名单？（r380 教训）

## 3. 门禁
- `make -C mt-vgpu-guest check-offline`：**472 Python + 299 C 全绿**（+9 新测试）。
- 反向验证（三项全部通过）：
  - RV1：`mt_pvr_bridge.c` 注入 `vm->ranges = 0;` → T1 FAIL → 还原。
  - RV2：`MT_FW_DM_TA` 改 `2U` → T2 FAIL → 还原。
  - RV3：`test_diagnostics.py` 注入 `rmmod -f foo` → T3 FAIL → 还原。
- `make kernel` 未跑（本轮无内核代码改动；新增仅测试脚本 + shell）。

## 4. 诚实边界
- T1/T2 是源码静态扫描：能拦截 in-tree 代码引入新违规；**一次性探针模块**（如 r380 的 `mt_live_3d_probe.c`，gitignored）不受扫描约束——靠 pre-live 清单第 1/2 条人工执行。
- T2 的 `(1,100)`（`mt_live_marker` 可经模块参数打到 dm=2/3）：参数可变，静态无法钉死；清单要求 live 简报中声明实际 dm。
- `safe_rmmod.sh` 依赖 `lsmod` 第三列为数字 refcount；"Used by" 为模块名列表时按持有者拒绝（exit 3），语义仍为"不强卸"。
- 本轮零硬件触碰：未加载/卸载模块，未提交 GPU 工作。

## 5. 交付物
- 本报告（`mt-vgpu-guest/reports/r394-live-safety-tests.md`）
- `mt-vgpu-guest/tests/test_vm_init_integrity.py`、`test_opcode_whitelist.py`、`test_pre_live_safety.py`
- `mt-vgpu-guest/scripts/safe_rmmod.sh`（0755）
- `AGENTS.md` §5/§6/§8 三处各加一行
