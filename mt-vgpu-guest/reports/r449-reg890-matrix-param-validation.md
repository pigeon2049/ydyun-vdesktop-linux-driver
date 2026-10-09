# r449：reg890/0x898 状态机完整矩阵 + 参数验证全覆盖（离线）

**结论：r449 新增 20 个测试/门禁，覆盖 reg890 完整状态矩阵、6 条参数验证规则、0x898 严格检查与正常路径回归。589 Python + 781 C 全绿，kernel W=1 零警告。反向验证：r445 代码上 6/20 精确 FAIL（均为 reg890 接受性测试）。纯离线，零硬件触碰。**

## 新增测试

**新文件** `mt-vgpu-guest/tests/guest/test_probe_890_matrix.py`（20 tests，4 个测试类）：

### 1. TestReg890StateMatrix（8 tests）——状态机完整覆盖

文档化 0x890 在全部 probe 路径检查点的接受矩阵：

| 位置 | recover=0,trial=0 | recover=0,trial=1 | recover=1,trial=0 | recover=1,trial=1 |
|---|---|---|---|---|
| mt_probe / mt_read_device_info | {0} | {0,2} | {1,2} | 非法（-EINVAL） |
| mt_probe_channels（recover 分支） | — | — | {1,2} | — |
| mt_reserve_memory（recover=0 恒定） | {0,2} | — | — | — |
| mt_snapshot_memory | {0,2} | — | {1,2}（假设） | — |
| 测试辅助（destructive） | {0} 严格 | — | — | — |
| retained-kick（实验） | — | — | {1} 严格 | — |

- 验证 4 个修复位置的模式正确性
- 验证测试辅助保持 `0x890==0` 严格要求（破坏性测试需干净状态，刻意保留）
- 验证 retained-kick 保持 `0x890==1` 严格要求（实验路径，刻意窄范围）
- `test_no_bare_890_eq_1_in_recover_path`：全文件仅允许 1 处裸 `!= 1`（即 retained-kick），防止未来回归

### 2. TestParamValidationComplete（6 tests）——参数验证全覆盖

r446/r448 仅锁定了 2 个 recover_channels 子条件；本轮锁定全部 6 条 -EINVAL 规则：

1. `refresh_osid && !recover_channels` → -EINVAL
2. `runtime_context && (!query_info || !load_firmware)` → -EINVAL
3. `recover_channels` 排斥 8 个冲突标志（!query_info、!probe_rpc、trial_connect、reserve_memory、test_memory_write、prepare_resources、load_firmware、test_firmware_upload、runtime_context）→ -EINVAL
4. `(test_memory_write || prepare_resources) && !reserve_memory` → -EINVAL
5. `(load_firmware && !prepare_resources) || (test_firmware_upload && !load_firmware)` → -EINVAL
6. `trial_connect` 需求（query_info+load_firmware+probe_rpc，排斥 test_firmware_upload/test_memory_write）→ -EINVAL

### 3. TestChannelReady0898（3 tests）——0x898 严格检查

- 5 个位置（mt_probe、mt_read_device_info、mt_snapshot_memory、mt_probe_channels、mt_reserve_memory）均保持 `0x898==1` 严格检查
- 全文件无 `0x898` 的 `==2` 式例外（与 0x890 不同，刻意）
- **[TO-VALIDATE]**：0x898 跨冷重启持久性未知。若固件某天以 `0x898!=1` 启动，所有路径将返回 -EBUSY 且无例外（不像 0x890==2）。已在测试 docstring 记录，未来出现 `0x898!=1` 的 -EBUSY 时可直接诊断，无需重新推导。

### 4. TestNormalPathRegression（3 tests）——正常路径回归

- `reg890==1` + `recover_channels=1` 仍被接受（r448 模式）
- `reg890==0` + `recover_channels=0` 仍被期望（mt_probe）
- `reg890==0` 在 mt_reserve_memory 仍被接受

## 反向验证

- **新代码**：20/20 通过
- **回退到 r445**（`git show a349020:...`）：**6/20 精确 FAIL**
  - FAIL 的 6 个均为 reg890 接受性测试（mt_probe、mt_read_device_info、mt_snapshot_memory、mt_probe_channels 的矩阵模式 + 2 个相关）
  - PASS 的 14 个为参数验证（6）、0x898（3）、回归（2 中 1 个）等——符合预期，这些逻辑在 r445 已存在，本轮是锁定而非新增
- **恢复后**：20/20 通过

## 门禁

- `make -C mt-vgpu-guest check-offline`：**589 Python + 781 C 全绿**（569+20 新，1 skipped，1 pre-existing ResourceWarning）
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 无生产代码变更（本轮纯测试）

## 安全

- 纯离线轮，未触碰硬件，未加载模块
- 工作区干净（仅新测试文件 untracked）

## 诚实边界

- 测试为源码模式测试（regex），非运行时行为测试；实际 reg890 值未在硬件上测量
- [TO-VALIDATE] 0x898 持久性：基于代码分析，非实测
- 本轮未链入下一轮；r450（待用户确认）：trial 重建 → `+0x120=0x1` 活体
