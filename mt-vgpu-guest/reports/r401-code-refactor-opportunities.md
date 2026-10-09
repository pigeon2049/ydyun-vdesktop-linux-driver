# r401: 代码重构机会分析——7 个机会按优先级排序

**结论**：30 轮迭代后，代码库整体健康（r383/r398 清理彻底，零死代码）。找到 7 个重构机会，**Top 2 值得做**（测试 helper 提取、TA/3D 提交统一），其余按需。

## 优先级排序

| # | 机会 | 收益 | 风险 | 优先级 |
|---|---|---|---|---|
| 1 | 测试 helper 提取（47 文件重复 ROOT） | 高 | 低 | **P0 做** |
| 2 | TA/3D submit 统一（~70% 重复） | 高 | 中 | **P1 做** |
| 3 | `pvr_translator_prepare_locked` 拆分（294 行） | 中 | 中 | P2 可选 |
| 4 | `pvr_bridge_dispatch` 拆分（168 行） | 中 | 低 | P2 可选 |
| 5 | 魔法超时值命名常量 | 低-中 | 低 | P3 顺手 |
| 6 | Kernel 头文件重组（80 平铺） | 中 | 高 | 暂缓（r400 已定） |
| 7 | ENOTTY vs EOPNOTSUPP | 无 | 无 | 非问题 |

## 机会详情

### 1. 测试 helper 提取 [P0]

**现状**：47 个测试文件重复 `ROOT = Path(__file__).resolve()...`，且有 2 种不一致写法：
- `.parent.parent.parent`（1 文件）
- `.parents[2]`（13 文件，其余用其他变体）

无共享 helper 模块（仅空 `__init__.py`）。

**方案**：新建 `tests/helpers.py`：
```python
from pathlib import Path
def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]
def kernel_header(name: str) -> Path: ...
```
47 文件改为 `from helpers import repo_root`（或 `from tests.helpers import ...`）。

**收益**：消除重复，统一写法；r400 移动目录时曾需批量改 53 文件的 `parents[1]`→`parents[2]`，有 helper 只需改一处。
**风险**：低。机械替换，`check-offline` 全绿即验证。
**工作量**：约 1 轮（r402）。

### 2. TA/3D submit 统一 [P1]

**现状**：`mt_marker_submit_ta_work()`（139 行）vs `mt_marker_submit_3d_work()`（~100 行），diff 显示约 70% 相同。相同部分：参数校验框架、fence 依赖等待（5000ms）、`s->lock` 临界区、wire_id 分配（63 上限、`0xffffffff` 溢出检查）、`kzalloc` 失败处理、提交后 bookkeeping。

差异仅：DM 号（3 vs 2）、opcode（0x66 vs 0x68）、params 结构（`ta_params` vs `d3_params`）、包构造（`mt_fw_ta_marker_command` vs `mt_fw_3d_command`）、门控（TA 常开 vs 3D 门控）、完成码（0x100 vs 0）。

**方案**：提取公共 `mt_marker_submit_work(s, dm, ops, ...)`，其中 `ops` 含：
- `build_command(packet, wire_id, pid, params)`
- `validate_params(params)`（TA 的 D5/D8 vs 3D 的非空检查）
- `complete_code` / `gate_check()`

TA/3D 各保留薄 wrapper（~20 行）。

**收益**：高。新增 DM（如未来 TDM 真实化）只需写 ops，不复制 100 行框架；bug 修复只需改一处。
**风险**：中。触碰固件提交路径，需 `check-offline` + `make kernel` + 反向验证。T2 白名单测试可捕获 opcode/DM 错配。
**工作量**：约 1-2 轮。建议在下次改动提交路径时顺手做，不单独立项也可。

### 3. `pvr_translator_prepare_locked` 拆分（294 行）[P2]

**现状**：`kernel/recovery/mt_pvr_bridge.c:1936-2230`，294 行。功能：构建 DM 上下文（device 获取、BO 分配、context 创建）。

**方案**：按阶段拆分为 3 个静态 helper：
- `pvr_translator_setup_device()`（device/owner 获取）
- `pvr_translator_alloc_bos()`（BO 分配）
- `pvr_translator_create_contexts()`（context 创建）

**收益**：中。可读性提升，错误路径更清晰。
**风险**：中。初始化逻辑交错，拆分需小心资源回滚顺序。现有行为无测试覆盖（仅 live 验证）。
**建议**：可选。下次改动此函数时再拆。

### 4. `pvr_bridge_dispatch` 拆分（168 行）[P2]

**现状**：`kernel/recovery/mt_pvr_bridge.c:5050-5218`，168 行。主 dispatch switch，按 bridge group（0x82/0x88/0x89/0x2 等）分发。

**方案**：按 group 拆分为 `pvr_dispatch_pvr()`, `pvr_dispatch_sync()`, `pvr_dispatch_tdm()` 等，主函数只做 group 路由（~20 行）。

**收益**：中。可读性；新增 bridge 命令时定位更快。
**风险**：低。纯 switch 拆分，行为不变，编译即验证大半。
**建议**：可选。低 hanging fruit，可与机会 5 一起做。

### 5. 魔法超时值命名常量 [P3]

**现状**：
- `msecs_to_jiffies(5000)` ×2（`mt_marker_fence.h:441,614`，fence 等待）
- `msecs_to_jiffies(250)` ×2（`mt_rpc_service.h:73,94`，RPC 重试）
- `msecs_to_jiffies(60000)` ×1（bridge.c:3848，60s 超时）

**方案**：定义 `MT_FENCE_WAIT_MS 5000`、`MT_RPC_RETRY_MS 250`、`MT_BRIDGE_OP_TIMEOUT_MS 60000`。

**收益**：低-中。语义清晰，调参时一处改。
**风险**：低。纯重命名。
**建议**：顺手做，可并入机会 2 或 4 的轮次。

### 6. Kernel 头文件重组（80 平铺）[暂缓]

r400 已评估：80 头文件交叉引用复杂，移动风险高收益低。维持现状，待自动化重构工具。

### 7. ENOTTY vs EOPNOTSUPP [非问题]

分析确认是**有意区分**：
- `ENOTTY`：dispatch 层"无此操作"（未知 bridge function，ioctl 惯例）
- `EOPNOTSUPP`：功能层"操作存在但当前配置不支持"（门控关闭、TO-VALIDATE）

15 vs 15 的用量是巧合。用法规一致，无需统一。

## 死代码检查

- `kernel/recovery/mt_pvr_bridge.c`：45+ 静态函数全数有引用（初筛 7 个"未引用"经核查均为函数指针/结构体初始化引用，如 `.open = pvr_open`）。**零死代码**。
- r383（probe TA VM）、r398（per-file VM）清理彻底，`grep` 确认无残留引用。

## 长函数 Top 10（bridge.c，5557 行）

| 行数 | 函数 | 行号 | 备注 |
|---|---|---|---|
| 294 | `pvr_translator_prepare_locked` | 1936-2230 | 见机会 3 |
| 185 | `pvr_translator_fire_work` | 4722-4907 | workqueue，逻辑内聚，暂不动 |
| 168 | `pvr_bridge_dispatch` | 5050-5218 | 见机会 4 |
| 139 | `pvr_mmap` | 5315-5454 | mmap 处理，分支多但内聚，可不动 |
| 72 | `pvr_file_release` | 892-964 | V4 清理，可接受 |
| 67 | `pvr_cmd_pmr_map` | 3106-3173 | 可接受 |
| 65 | `pvr_gpu_vm_ensure` | 425-490 | 可接受 |
| 65 | `pvr_cmd_kicksync_submit` | 2591-2656 | 可接受 |
| 53 | `pvr_pmr_new` | 1158-1209 | 可接受 |
| 51 | `pvr_translator_tqx_slices` | 2324-2375 | 可接受 |

仅前 4 超过 100 行，其中 2 个（fire_work、mmap）内聚尚可，不建议拆。

## 测试冗余

- 47 文件重复 ROOT preamble → 见机会 1。
- C 测试（`tests/c/`，53 文件）各有独立 main，无共享框架，但 C 测试本就轻量，不建议抽象。
- Python 测试逻辑无明显重复（各测不同 wire 结构/行为）。

## 建议路线图

1. **r402**：机会 1（测试 helper 提取）——低风险高收益，约 1 轮。
2. **机会 2+5**：下次触碰提交路径时顺手做（或单独 1-2 轮）。
3. **机会 3+4**：可选，低优先级。
4. **机会 6**：暂缓。

## 门禁

- `make -C mt-vgpu-guest check-offline`：474 Python + 299 C 全绿（本轮无代码改动）
- 本轮零硬件触碰，纯离线分析
