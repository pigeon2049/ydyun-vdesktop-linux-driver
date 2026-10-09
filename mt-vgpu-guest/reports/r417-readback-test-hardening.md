# r417: 回读路径测试加固完成——测试 +19/+326，发现并修复两处门控 latent build break（离线）

> Round r417 (2026-10-09). **Offline, zero hardware touched.** 用户指示"稳妥推进 先加测试"——活体前加固离线测试，避免低级错误导致冷重启。

## Conclusion

**回读路径测试加固完成；测试过程发现并修复了两处真实 latent build break，均与门控组合有关。**

- 新增测试：Python **+19**（522 总，1 skip），C **+326 checks**（625 总）。
- Bug 1（门控不一致）：`0x82:0xFD` dispatch `case` 仅被 `#if MT_TA_READBACK_DEBUG`
  包裹，而 `pvr_cmd_ta_readback` 定义需要 `DEBUG && REAL`——`(DEBUG=1, REAL=0)`
  构建报 `implicit declaration of function 'pvr_cmd_ta_readback'`。已修复为双门控，
  旧门控构建失败、新门控构建干净（实证）。
- Bug 2（缺前向声明）：`(DEBUG=1, REAL=1)` 构建报 `implicit declaration of
  function 'mt_ta_submit_real'`——handler（~5204 行）在定义（~5737 行）之前调用，
  无前向声明。该组合历史上从未编译成功过（r416 仅验证到 intentional assert）。
  已补前向声明，`(1,1)` 构建干净。
- 重构：`mt-ta-readback.c` 像素校验逻辑提取为 `userspace/ta_readback_analyze.h`
 （行为锁定 r416，含 black 计入 distinct 的 quirk）；工具 ENOTTY 提示现注明双门控。
- 门禁：`check-offline` **522 Python + 625 C 全绿**，`make kernel` W=1 零警告，
  反向验证 4 项通过。

## New tests

### Python — `tests/ta/test_ta_readback.py` (+19)

| Class | Tests | Coverage |
|---|---|---|
| `TestTargetBoLifecycle` | 6 | create 绑定位置（11-BO 循环后、exec 前）；槽位 11 无冲突（`MT_GFX_CONTEXT_BO_COUNT`=11）；destroy 逆序（exec→11 BOs→target→VM）；bind 失败无泄漏；rollback 复用 `mt_render_context_destroy`（半初始化安全）；VA 槽位表达式 |
| `TestDebugIoctlValidation` | 6 | 坏 ctx→`-EINVAL`；`!target_ready`→`-ENODEV`；width/height/n_entries 透传给校验；**门控一致性**（case 与函数同门）；门控关闭→`-ENOTTY`；`mt_ta_submit_real` 前向声明存在且在 handler 之前 |
| `TestTaSubmitRealTargetVa` | 4 | 空指针守卫；`n_entries` 越界拒收（0 / >9）；`target_va` 透传给 `mt_ta_real_buffer_build`；staging BO 空检查 |
| `TestReadbackAnalyzeUnit` | 3 | `ta_readback_analyze.h` 存在；工具引用该头文件；ENOTTY 提示提及双门控 |

### C — `tests/c/pvr_bridge_core_test.c` (+4 functions, +326 checks)

| Function | Coverage |
|---|---|
| `test_ta_entry_simple_build_validation` | NULL/零/越界拒收；Q2 打包 [MEASURED]；q4=`w*h-1`；其余 qwords 清零 |
| `test_ta_entry_q0_flag_or` | `va\|0x48000000000` 正确；低 32 位地址位不受污染；flag 幂等；常量仅占 bit39+42 |
| `test_ta_real_buffer_build_target` | `target_va=0` 兼容（Q0=0）；非零透传到每个条目；尾部 280B 清零；9 条目恰填满 360B |
| `test_ta_readback_analyze` | 全零（black quirk：distinct=1）；alpha 忽略；红/绿/蓝/黑；21 色 cap 16；空输入安全 |

### r415 测试适配

`tests/ta/test_ta_real.py` 的 2 个测试（`test_submit_real_in_bridge`、
`test_submit_real_no_hardcode`）改锚定 `__maybe_unused` 定义（前向声明出现后原
`find()` 会命中声明）。意图不变。

## Code changes

| File | Change |
|---|---|
| `kernel/recovery/mt_pvr_bridge.c` | 0xFD case 门控 → `#if MT_TA_READBACK_DEBUG && MT_TA_REAL_PACKET`；新增 `mt_ta_submit_real` 前向声明（r417 注释） |
| `userspace/ta_readback_analyze.h` | **新建**：`ta_readback_analyze()` + `struct ta_readback_stats`，行为与 r416 工具逐行一致 |
| `userspace/mt-ta-readback.c` | 改用共享头文件；ENOTTY 提示注明双门控；注释修正 |

## Reverse validations

1. **TDD 红→绿**：实现前 4 个新测试 FAIL（`test_0xfd_gate_consistency`、
   `test_analyze_header_exists`、`test_tool_uses_shared_analyze`、
   `test_tool_enotty_mentions_both_gates`），实现后全绿。
2. **Q0 OR→XOR**（临时破坏 `mt_ta_entry_simple_set_target`）：C 二进制
   `test_ta_entry_q0_flag_or` FAIL → 还原后绿。
3. **像素 R-shift 16→8**（临时破坏新头文件）：`test_ta_readback_analyze` FAIL
   → 还原后绿。
4. **门控组合构建实证**（临时中和 intentional `static_assert`，事后还原）：
   旧 case 门控 + (1,0) → `implicit declaration of 'pvr_cmd_ta_readback'`（bug 确认）；
   新门控 + (1,0) → 干净；(1,1) 初报 `implicit declaration of 'mt_ta_submit_real'`，
   补前向声明后干净。

## Honest boundaries

- Q0 flag `0x48000000000` 仍为 [INFERRED]（r410）；位操作测试只验证"按设计打包"，
  不验证固件语义。
- `(DEBUG=1, REAL=*)` 构建验证依赖临时中和两个 intentional `static_assert`
 （防误提交门控开启的守卫）；已全部还原，默认构建不受影响。
- `q4 = (u64)(w * h - 1)` 中 `w*h` 为 u32 运算（大尺寸回绕）；64×64 下正确，
  未改动（超出本轮范围，已记录）。
- 活体验证仍未做；dummy TA 首次活体大概率全零像素（r416 结论不变）。
