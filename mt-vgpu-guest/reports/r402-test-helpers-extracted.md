# r402: 测试 helper 提取——75 文件 ROOT 路径逻辑统一（离线）

**结论**：新建 `tests/helpers.py`，75 个测试文件的 repo-root 路径表达式全部收敛到 `get_repo_root()` 等 7 个 helper；门禁 474+299 全绿，直接执行与 discover 双模式验证通过；反向验证确认 helper 损坏即被测试捕获。

## 背景

r401 P0 机会：测试文件重复 `ROOT = Path(__file__).resolve()...`（r401 计数 47，实测 75——除 ROOT 定义外，还有 SOURCE/GUEST/TOOL/HEADER/BRIDGE/REPO/SCRIPT 等内联 `parents[2]` 用法），且有 2 种不一致写法（`.parent.parent.parent` ×17、`.parents[2]` ×29+）。r400 移动目录时曾被迫批量改 53 个文件的 `parents[1]`→`parents[2]`——有 helper 的话只需改一处。

## 实现

**新模块 `tests/helpers.py`**（62 行）：
- `get_repo_root()` —— 由本文件自身位置推导（`tests/helpers.py` → `parents[1]`），与调用方目录深度无关；测试文件再搬家无需改路径
- `get_tests_dir()` / `get_kernel_dir()` / `get_scripts_dir()` / `get_reports_dir()` / `get_build_dir()`
- `get_kernel_header(name)` —— `kernel/` 下头文件便捷路径

**重构 75 文件**（机械替换，`git diff` 可审）：
- `Path(__file__).resolve().parents[2]` → `get_repo_root()`（56 文件）
- `Path(__file__).resolve().parent.parent.parent` → `get_repo_root()`（17 文件）
- `Path(__file__).parents[2]`（无 resolve，2 文件）→ `get_repo_root()`
- `KERNEL = ...parents[2] / "kernel"` → `KERNEL = get_kernel_dir()`（3 文件）
- `SCRIPTS = ...parents[2] / "scripts"` → `SCRIPTS = get_scripts_dir()`（1 文件）

**双模式 import 保持**（77 文件全有 `__main__` 块，直接执行是受支持路径）：
```python
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root
```
discover 模式下 `sys.path` 已含 repo 根，insert 无害；直接 `python3 tests/pvr/test_x.py` 时靠它找到 `tests.helpers`。`ROOT = get_repo_root()` 之后所有 `ROOT / "kernel"` 等下游用法零改动。

**修复 1 处脚本缺陷**：3 个文件的原 `import sys` 位于 ROOT 行之后，bootstrap 若省略 `import sys` 会 `NameError`（check-offline 首轮 3 errors）。改为 bootstrap 恒自带 `import sys`（后续重复 import 无害）。

**未动**：`SOURCE.parents[2]`（由 SOURCE 派生非 `__file__`）、`Path(__file__).resolve().parent`（文件级相对）、WRAPPER 行、`tests/c/`。

## 门禁与验证

- `make -C mt-vgpu-guest check-offline`：**474 Python + 299 C 全绿**
- 直接执行：`python3 tests/pvr/test_pvr_wire_sizes.py` OK；`python3 -m unittest tests.misc.test_pre_live_safety` OK
- **反向验证**：`get_repo_root()` 改为 `parents[2]`（错一层）→ `test_pvr_wire_sizes` 报 `FileNotFoundError: .../scripts/build-stage-b-bridge-requirements.py`（路径错位即被捕获）；还原后全绿
- `make kernel` 未跑（本轮零内核改动；纯测试重构）

## 诚实边界

- 反向验证过程中遇到 `__pycache__` 陈旧 `.pyc` 导致"还原后仍失败"假象（sed/cp 同秒同尺寸，mtime 粒度内判等）：清 `tests/*/__pycache__` 后恢复正常。属验证手法问题，非重构缺陷；教训已记入本报告。
- 3 文件残留重复 `import sys`（后置的原 import 未删）：无害，未动（最小 churn）。
- `tests/helpers.py` 自身 docstring 含字面 `parents[2]`（文档），T1/T2 扫描针对 `kernel/**` 与白名单，不受影响。

## 文件清单

- 新：`mt-vgpu-guest/tests/helpers.py`
- 改：75 个 `tests/{pvr,ta,guest,render,misc}/*.py`（import bootstrap + 表达式替换）
