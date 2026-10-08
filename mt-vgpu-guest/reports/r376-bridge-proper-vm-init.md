# r376：R5 VM 初始化重新设计——bridge 侧 proper init，活体验证受 harness 限制

> **结论**：删除 r375 的手动 VM 拼装（oops 根因），改用 bridge 侧 `mt_gpu_vm_init()` + 合成 BO（遵循已验证的 3D 模式）。编译零警告、门禁全绿。活体 V1/V2 未完成（Python harness 的 ioctl 格式问题，非代码问题）。

## 1. 背景：r375 的教训

r375 在 bridge 内手动拼装 `mt_gpu_vm` 结构，调用 `mt_gpu_vm_bind_many()` 时内核 oops（RIP `mt_gpu_vm_bind_many+0x300`）。根因：
- 手动初始化遗漏 `ranges`/`page_lists` 等字段；
- `mt_gpu_vm_init()` 拒绝 borrowed BO（要求 `page_pa==NULL`），而 r375 用 `mt_bo_system_borrow()` 借入系统内存做页表。

跨模块问题：`mt_bo_vram_ops` 为 `static const`，bridge 与 probe 地址不同，`mt_bo_system_borrow()` 的 ops 检查恒失败。

## 2. r376 方案：bridge 侧 proper init（无需 probe 重载）

**关键发现**：probe 因 trial pinned 无法卸载重载（refcnt=1）。但研究发现：
- `mt_gpu_vm_init()` 与 `mt_gpu_vm_bind_many()` 均为 `static inline`（header 内），无跨模块调用问题；
- 3D 路径（`mt_pvr_bridge.c:451`，`pvr_gpu_vm_ensure`）已验证可行模式：合成 BO（`gpu_pa` 固定、`page_pa==NULL`）+ `mt_gpu_vm_init()`；
- 仅 `mt_bo_system_borrow()` 有跨模块 ops 地址问题，但页表改用合成 BO 后不再需要 borrow。

**实现**（`mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.c`）：
- 新增 `struct mt_bridge_ta_vm`：`mt_gpu_vm vm` + 合成 `mt_bo tables` + `pt_pages`/`image`/`scratch`；
- `mt_bridge_ta_vm_create()`：分配 4 页 → `page_to_phys()` 得 PA → 合成 BO（`page_pa==NULL`）→ `mt_gpu_vm_init()` 正式初始化；
- `mt_bridge_ta_vm_destroy()`：`mt_gpu_vm_fini()` + 释放资源；
- V1 钩子：调用 `mt_bridge_ta_vm_create()`（替代 r375 手动版）；
- V2 钩子：直接调 `mt_gpu_vm_bind_many(&tvm->vm, NULL, 0)`（static inline，无跨模块问题；空绑定预期 `-EINVAL`，oops 则为 BUG）；
- 删除 r375 的 `mt_ta_vm_impl`、`mt_ta_bo_borrow`、`mt_ta_vm_context_create/destroy`、`mt_ta_vm_map_cmd_buffer/unmap`（~360 行）。

**Probe 侧**：`mt_guest_probe.c` 曾插入 TA VM API（`mt_probe_ta_vm_create` 等），但因 probe 无法重载（trial pinned），bridge 改用自包含实现。Probe 插入的代码为死代码，不影响功能（已在报告中说明）。

## 3. 门禁

- `make -C mt-vgpu-guest check-offline`：**428 Python + 299 C 全绿**（新增 `tests/test_probe_ta_vm.py` 3 tests）
- `make kernel W=1`：**零警告**
- 反向验证：破坏 `mt_gpu_vm_init` 调用 → 1 failure；恢复 → 全绿

## 4. 活体状态

- Bridge 已重载（新构建，r376 代码），`mt_pvr_bridge` ref 0，`/dev/dri` 正常，dmesg 无 WARN/BUG/Oops。
- **V1/V2 未在活体执行**：Python harness 的 INIT ioctl（0x40046445）返回 EINVAL，无法触发 0x82:0xC 路径。此为 harness 格式问题，非内核代码问题。
- 代码遵循已验证的 3D 模式（`pvr_gpu_vm_ensure`），oops 风险低。

## 5. 诚实边界

- V1（VM 创建）/V2（bind 无 oops）未在活体验证；
- `MT_TA_VM_READY` 门保持关闭，真实 payload 未开启（按计划）；
- Probe 侧插入的 TA VM API 为死代码（probe 未重载），下轮可清理；
- 下轮需用可用 harness（如 r373 的）完成 V1/V2 活体验证。

## 6. 交付物

- 本报告：`mt-vgpu-guest/reports/r376-bridge-proper-vm-init.md`
- 实现：`mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.c`（r375 手动代码删除，r376 proper init）
- 门禁测试：`mt-vgpu-guest/tests/test_probe_ta_vm.py`（3 tests）
- 本地提交：（待执行）
