# r383: probe 侧 r376 死代码清理完成（离线，零警告）

## 结论

r376 插入的 probe 侧 TA VM API 死代码已全部删除（mt_probe_ta_vm.h 29 行 + mt_guest_probe.c 中 122 行：struct mt_probe_ta_vm、mt_probe_ta_vm_create/destroy/bind、mt_probe_bo_borrow 及 4 个 EXPORT_SYMBOL_GPL）。make kernel W=1 零警告——r382 报告的 4 个 pre-existing 警告已消除。本轮零硬件触碰，probe 未重载（trial pinned）。

## 1. 删除清单

- kernel/mt_probe_ta_vm.h：整个文件（git rm），29 行
- kernel/mt_guest_probe.c：struct mt_probe_ta_vm 定义（~12 行）
- kernel/mt_guest_probe.c：mt_probe_ta_vm_create()（~55 行）
- kernel/mt_guest_probe.c：mt_probe_ta_vm_destroy()（~12 行）
- kernel/mt_guest_probe.c：mt_probe_ta_vm_bind()（~8 行）
- kernel/mt_guest_probe.c：mt_probe_bo_borrow()（~15 行）
- kernel/mt_guest_probe.c：4x EXPORT_SYMBOL_GPL（4 行）
- kernel/recovery/mt_pvr_bridge.c：更新过时注释（r376 probe-side 描述改为 bridge-side 现实，~8 行）

## 2. 零引用验证

删除前 grep 确认：

- mt_probe_ta_vm.h 无任何文件 include（孤立头文件）。
- 4 个函数 + struct 仅在定义处出现，无调用者（bridge 已改用自包含实现，r376）。
- mt_guest_probe.mod.c 中的符号表条目为构建生成物，重建后自动消失（已验证 nm mt_guest_probe.ko 无残留符号）。
- tests/test_probe_ta_vm.py（r376 门禁）保留：它验证 bridge 不调用 probe API（回归 guard），删除 probe 侧代码不影响其有效性。

## 3. 门禁

- make -C mt-vgpu-guest check-offline：430 Python + 299 C 全绿。
- make kernel W=1：零警告（r376 死代码的 4 个 pre-existing 警告已消除）。
- 反向验证：grep 零引用 + nm 确认符号消失 + 构建通过，证实删除的是死代码。

## 4. 安全

- 未重载 probe（trial pinned，无法卸载；源码清理不影响运行中系统）。
- 未重载 bridge；未重启。
- 工作区除预期修改外干净；两端 /tmp 零残留。

## 5. 诚实边界

- 本轮纯清理，无功能变更。
- Probe 侧死代码的物理移除需下次 probe 重载（冷重启后）才在运行系统中生效；当前运行中的 probe 仍含旧符号（无害，未被调用）。
