# r403：TA/3D submit 公共 ops 框架提取完成——重复框架逻辑两份合一，行为零变更（离线）

## 结论

`mt_marker_submit_ta_work`（85 行）与 `mt_marker_submit_3d_work`（103 行）中约 65 行完全相同的提交框架逻辑（校验骨架 / fence 等待 / wire_id 管理 / 错误回滚 / 成功消费）已提取为单一公共函数 `mt_marker_submit_engine_work()`，引擎差异收敛为 `struct mt_marker_submit_ops`（dm + 3 个 hook）。两函数变为 thin wrapper，行为 100% 一致（校验顺序、错误码、加锁边界、wire_id 永不复用语义逐行对照无差）。T2 白名单测试随之更新（dm 钉死位置从 wrapper 局部变量移到 ops 表，安全不变量逐条保留），反向验证证明更新后的测试仍能捕获违规。门禁 474+299 全绿，`make kernel` W=1 零警告。零硬件触碰。

## 1. diff 确认：相同 vs 差异（r401 的 ~70% 数据落实）

旧代码（`kernel/mt_marker_fence.h`，r402 HEAD）：TA 提交函数行 403–487（85 行），3D 提交函数行 572–674（103 行）。

| 部分 | TA | 3D | 结论 |
|---|---|---|---|
| NULL 检查 | `if (!s \|\| !work \|\| !out) return -EINVAL;` | 相同 | 相同 |
| job 状态 / context / dm / family 校验 | 相同骨架，`dm` 不同 | 相同骨架 | 相同（除 dm） |
| 引擎 params 校验 | kick_pr/D8 拒收（D5/D8） | 空包拒收（r381） | **差异** |
| ready/can_submit | 相同 | 相同 | 相同 |
| fence 等待（5000ms，无锁） | 15 行相同 | 15 行相同 | 相同 |
| 加锁：63 cap / wire_id 永不复用 / kzalloc | 25 行相同 | 25 行相同 | 相同 |
| 构包 | `mt_fw_ta_marker_command`（0x66） | `mt_fw_3d_command`（0x68） | **差异** |
| params 入库 | `m->ta_params` | `m->d3_params` | **差异** |
| list_add / try_submit / 失败回滚 | 20 行相同 | 20 行相同 | 相同 |
| 成功消费（job 置空/context/pool_slices/out） | 8 行相同 | 8 行相同 | 相同 |
| DM | `MT_FW_DM_TA` (3) | `MT_FW_DM_3D` (2) | **差异** |
| 3D 编译期门控 | 无 | `#if !MT_3D_SUBMIT_GATE` 先于 NULL 检查 | **差异**（顺序保留） |

差异仅 5 处：DM、门控、params 校验、构包函数、params 入库槽。其余 ~65 行逐行相同。

## 2. 重构设计

```c
struct mt_marker_submit_ops {
    u32 dm;                                          /* MT_FW_DM_TA / MT_FW_DM_3D */
    int  (*validate_params)(const void *params);     /* D7 前的引擎校验 */
    void (*build_command)(void *packet, u32 wire_id, u32 pid,
                          const void *params);       /* 固件包构造 */
    void (*store_params)(struct mt_marker_fence *m, const void *params);
};

static int mt_marker_submit_engine_work(s, ops, job, pcontext,
                                        pool_slices, check_fence, params, out);
```

- 公共函数实现校验顺序 → 无锁 fence 等待 → 加锁分配/提交/回滚/消费，与旧代码逐行一致；`dm` 取自 `ops->dm`。
- `mt_ta_submit_ops`：`.dm = MT_FW_DM_TA`，hook 为 D5/D8 校验、`mt_fw_ta_marker_command` 构包、`ta_params` 入库。
- `mt_3d_submit_ops`：`.dm = MT_FW_DM_3D`，hook 为空包校验、`mt_fw_3d_command` 构包、`d3_params` 入库；**整个 3D hooks+ops 块包在 `#if MT_3D_SUBMIT_GATE` 内**（门关时零编译、零 `-Wunused-const-variable` 警告）。
- thin wrapper 只做 NULL 检查（D7）后调公共函数；3D wrapper 的 `#if !MT_3D_SUBMIT_GATE` 先于 NULL 检查的原有顺序**逐字保留**（行为差异仅理论上的 NULL+关门组合，无调用方传 NULL）。
- 完成码未进 ops：brief 建议的 complete_code 经核查属于**完成路径**（`mt_marker_complete_ta` vs 通用 `mt_marker_complete`），提交路径不涉及，已另行解耦，无需统一。

行数：旧 188（85+103）→ 新 208（ops 13 + 公共框架 101 + TA hooks/ops 30 + TA wrapper 13 + 3D hooks/ops 31 + 3D wrapper 20）。净 +20 行为 hook 样板；重复框架逻辑 ~65 行由两份变为一份；新增第三个引擎的边际成本从 ~100 行降至 ~35 行（一个 ops 表）。

## 3. T2 测试更新（`tests/misc/test_opcode_whitelist.py`）

r380 事故教训的文本耦合随重构必须更新，不变量逐条保留：

- `test_ta_opcode_pinned_to_dm3`：`"const u32 dm = MT_FW_DM_TA;"`（wrapper 局部）→ 检查 `.dm = MT_FW_DM_TA` 在 `mt_ta_submit_ops` 初始化式内；`mt_fw_ta_marker_command(` 调用点约束从"两 wrapper 之间"改为"必须落在 `mt_ta_submit_build` 函数体内"（定义除外）；新增 `mt_ta_submit_build` 全文件恰出现 2 次（定义 + ops 表引用），堵住 opcode 经其他路径流出的可能。
- `test_3d_opcode_pinned_to_dm2`：同理检查 `.dm = MT_FW_DM_3D`；ops 表及它命名的每个 hook 函数体内不得出现 `MT_FW_TA_OPCODE` / `mt_fw_ta_marker_command` / `mt_ta_submit_build`；3D wrapper 体保持 TA-free。
- `test_forbidden_pairs_have_no_symbolic_path`：扫描对象从"含 `const u32 dm = ...` 的函数"改为"per-engine ops 表"，递归检查其命名的 hook 函数体（通过 `static|inline ... name(` 定义匹配判定为函数）。
- 新增 helper `_function_span`（函数体起止偏移）、`_struct_init`（`name = { ... };` 配平提取）。

## 4. 反向验证（破坏 → 测试失败 → 还原）

- RV1：将 `mt_ta_submit_ops` 的 `.dm` 篡改为 `MT_FW_DM_3D` → `test_ta_opcode_pinned_to_dm3` **FAIL**（符合预期）。
- RV2：在 `mt_3d_submit_build` 内引用 `MT_FW_TA_OPCODE`（r380 式违规）→ `test_3d_opcode_pinned_to_dm2` 与 `test_forbidden_pairs_have_no_symbolic_path` **均 FAIL**（符合预期）。
- 还原后全绿。证明更新后的 T2 仍守住"TA opcode 永不上 DM2"与"dm 钉死"两条红线。

## 5. 门禁

- `make -C mt-vgpu-guest check-offline`：474 Python + 299 C **全绿**（含更新后的 T2）。
- `make kernel` W=1：**零警告**（中途出现 `mt_3d_submit_ops defined but not used`，将 3D hooks+ops 包入 `#if MT_3D_SUBMIT_GATE` 后消除；门关时死代码不编译，与旧行为一致）。
- 每改一处即编译验证；零硬件触碰。

## 6. 诚实边界

- 本轮纯离线重构；TA/3D 提交路径的行为等价性由逐行 diff 对照保证，未做活体回归（marker 路径活体覆盖在 r395/r397/r399 已验证，重构不改变其语义）。
- T2 仍为静态文本扫描；一次性探针模块不受约束（r394 已知边界，不变）。
- `MT_3D_SUBMIT_GATE=0` 期间 3D hooks/ops 不参与编译，T2 对其的检查为文本级——开门时需重新确认编译（届时门禁会覆盖）。

## 变更文件

- `kernel/mt_marker_fence.h`：提取 `mt_marker_submit_ops` + `mt_marker_submit_engine_work()`；TA/3D 变 thin wrapper。
- `tests/misc/test_opcode_whitelist.py`：T2 三测试适配 ops 表结构 + 2 个新 helper。
