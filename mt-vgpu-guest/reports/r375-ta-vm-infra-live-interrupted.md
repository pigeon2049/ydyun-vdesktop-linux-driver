# r375：R5 基础设施实现完成，活体验证因内核 oops 中断

> **结论**：per-file TA VM 上下文与命令缓冲映射基础设施实现完成（编译通过、门禁全绿），但活体 V1/V2 验证触发内核 oops（`mt_gpu_vm_bind_many`），根本原因是手动初始化的 VM 结构不完整。已定位两个跨模块障碍并部分修复。为安全已禁用 bind 路径。系统当前有 D-state 进程残留、bridge 无法卸载，需重启恢复。V1/V2 未在活体完成验证。

## 1. 实现落点

**文件**：`mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.c`（+~450 行）

### 1.1 数据结构
- `struct mt_pvr_file` 新增字段：`struct mt_ta_vm_context *ta_vm_ctx`（per-file，NULL 直到首次 TA 提交）
- `struct mt_ta_vm_impl`（私有）：`mt_ta_vm_context` + 页表页（4x4KiB）+ `page_pa` 数组 + VM image/scratch
- `struct mt_ta_mapping_impl`（私有）：`struct page **` + `page_pa` + `nr_pages`（mapping 生命周期内持有）

### 1.2 函数
- `mt_ta_vm_context_create(file)`：分配上下文、借入页表 BO（设备 store）、手动初始化 VM 结构
- `mt_ta_vm_context_destroy(ctx)`：释放 VM、BO、页表页
- `mt_ta_vm_map_cmd_buffer(ctx, user_va, size, out)`：8 步中的 2-5、7（pin → borrow → VA 计算 → 填 mapping；**bind 已禁用**）
- `mt_ta_vm_unmap_cmd_buffer(mapping)`：unpin + 释放 BO
- `mt_ta_bo_borrow()`（本地）：绕过跨模块 `mt_bo_vram_ops` 检查（见 §3.1）

### 1.3 集成点
- `pvr_cmd_musakickgfx2()`：D5 检查后，V1（首次创建上下文）、V2（若有 `p_ta_cmd` 则走映射流程验证，结果仅日志）
- `pvr_file_release()`：文件关闭时销毁 TA VM 上下文
- `MT_TA_VM_READY` 门保持关闭（r375 不启用真实 payload）

## 2. 门禁

- `make -C mt-vgpu-guest check-offline`：**425 Python + 299 C 全绿**
- `make kernel W=1`：**零警告**
- 新增 `tests/test_ta_vm_impl.py`（11 tests，standalone 模式）：验证函数存在、V1/V2 钩子、真实 borrow/bind/pin 引用、门关闭注释
- 反向验证：破坏期望符号 → FAIL；恢复 → PASS

## 3. 关键发现（活体调试中）

### 3.1 跨模块 `mt_bo_vram_ops` 地址不一致 [MEASURED]
- `mt_bo_vram.h` 将 `mt_bo_vram_ops` 定义为 `static const`，导致 bridge 和 probe 各有一份拷贝，地址不同。
- `mt_bo_system_borrow()` 中的 `s->ops != &mt_bo_vram_ops` 检查在跨模块调用时恒失败（-EINVAL）。
- **修复**：实现本地 `mt_ta_bo_borrow()`，复用 borrow 逻辑但使用 store 自带的 `s->ops`，跳过跨模块地址比较。

### 3.2 手动 VM 初始化不完整导致 oops [MEASURED]
- `mt_gpu_vm_init()` 拒绝 borrowed BO（要求 `page_pa==NULL`），故采用手动初始化 VM 结构。
- 但手动初始化遗漏了 `mt_gpu_vm_bind_many()` 所需的字段（`ranges`、`page_lists` 等），导致内核 oops（`RIP: mt_gpu_vm_bind_many+0x300`）。
- **临时措施**：r375 禁用 `bind_many` 调用，V2 仅验证 pin/borrow/VA 计算。完整 VM 初始化需重新设计（见 §5）。

### 3.3 页表 BO 的 `page_pa` 语义
- `mt_bo_system_borrow()` 设置 `bo->page_pa = m->page_pa`（借入的系统内存页表）。
- `mt_gpu_vm_init()` 要求 `tables->page_pa == NULL`（期望 VRAM BO）。
- 这是设计层面的不匹配：borrowed system RAM 不能直接作为 `mt_gpu_vm_init` 的输入。

## 4. 活体状态（中断）

- **已做**：一次计划内重载（r360 流程），新 bridge 上机成功。
- **V1 尝试**：TA kick 触发上下文创建，`pvr_session_acquire` 成功，但 `mt_bo_system_borrow` 返回 -EINVAL（§3.1，已修复）。
- **V2 尝试**：修复 borrow 后，`mt_gpu_vm_bind_many` 触发内核 oops，导致 python harness 进程进入 D-state，bridge refcount=3 无法卸载。
- **当前系统状态**：D-state 进程残留（PID 20219），`mt_pvr_bridge` 无法 rmmod，需重启（或关机/开机）恢复。
- **V1/V2 验证结论**：**未完成**——oops 中断了验证流程。

## 5. 诚实边界与建议

### 未验证项
- V1（per-file VM 上下文创建）：代码已写，但未在活体确认成功（borrow 修复后未重测）。
- V2（映射流程）：pin/borrow 逻辑已写，bind 已禁用，未活体验证。
- 真实 payload：`sealed` 门保持关闭，未开启（按计划）。

### 下一轮建议（r376）
1. **系统恢复**：重启（或关机/开机）清除 D-state 进程和残留引用。
2. **VM 初始化重新设计**：不应在 bridge 手动拼装 `mt_gpu_vm`。选项：
   - a) 在 probe 侧提供 VM 创建 helper（probe 持有完整初始化逻辑）；
   - b) 将 `mt_gpu_vm_init` 的 `page_pa` 检查放宽，允许 borrowed BO；
   - c) 为 TA 使用独立的、更简单的 VA 管理（不复用 `mt_gpu_vm`）。
3. **跨模块 ops 问题**：考虑将 `mt_bo_vram_ops` 改为非 static（EXPORT_SYMBOL），或提供跨模块安全的 borrow 接口。
4. 完成 V1/V2 活体验证后，再开启 `sealed` 门进入真实 payload。

## 6. 交付物

- 本报告：`mt-vgpu-guest/reports/r375-ta-vm-infra-live-interrupted.md`
- 实现：`mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.c`（+450/-10 行）
- 门禁测试：`mt-vgpu-guest/tests/test_ta_vm_impl.py`（11 tests）
- 证据：dmesg oops 日志（`reports/r375-dmesg-oops.txt`，0600）
- 本地提交：（待执行，需先恢复系统）

## 7. 安全记录

- oops 发生后立即停止活体操作，未尝试强制 rmmod 或其他恢复动作。
- 为防止再次 oops，已在源码中禁用 `bind_many` 调用（r375 安全措施）。
- 未触碰 `mt_guest_probe`（frozen）。
- 两端 /tmp 零残留（中转文件在 `build/traces/r375/`，gitignore）。
