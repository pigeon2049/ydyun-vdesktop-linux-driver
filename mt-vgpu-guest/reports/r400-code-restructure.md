# r400: 代码目录重构完成（A+B+D，C 暂缓）

## 结论
**Phase A（构建产物清理）、Phase B（tests 重组）、Phase D（顶层文档）完成。Phase C（kernel 头文件重组）评估为风险过高，记录为后续工作。**

## Phase A：构建产物清理
- `kernel/`、`kernel/recovery/`、`kernel/selftest/` 中的 .ko/.o/.mod/.cmd/.symvers 已清理（均为 gitignored）
- `make kernel` 重建验证通过：28 个模块，W=1 零警告
- `git status` 干净

## Phase B：tests 重组
**重构前**: `tests/` 平铺 77 个 .py + 53 个 .c/.h

**重构后**:
- `tests/c/` – 53 个 C/H 测试文件
- `tests/pvr/` – 35 个 test_pvr_*.py
- `tests/ta/` – 8 个 test_ta_*.py + test_3d_*.py
- `tests/guest/` – 7 个 test_guest_*.py + test_trial_*.py
- `tests/render/` – 3 个 test_render_*.py
- `tests/misc/` – 24 个其他测试

**修复**:
- 53 个文件的 `parents[1]` → `parents[2]`（repo root 定位）
- 17 个文件的 `.parent.parent` → `.parent.parent.parent`
- 53 个 C 文件的 `#include "../kernel/` → `"../../kernel/`
- 2 个文件的 `tests/*.c` → `tests/c/*.c`
- T3 测试 ALLOWLIST 路径更新
- Makefile：测试路径 + `unittest discover -t .`
- 全部使用 `git mv` 保持历史

**验证**: 474 Python + 299 C 全绿

## Phase C：暂缓
- 80 个头文件平铺，交叉引用复杂
- 移动风险高、收益低，记录为后续工作

## Phase D：顶层文档
- `README.md` 目录表已更新（tests/ 新结构）

## 门禁
- `make check-offline`: 474 Python + 299 C 全绿
- `make kernel` W=1: 零警告
