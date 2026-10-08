# r331：头文件 userspace 拼写收尾，L1+L2 全绿（离线，零硬件触碰）

- **结论**：r332 草稿（`mt_gpu_vm.h` / `mt_mmu_bootstrap.h` 的 header-local 分配/日志宏）收尾完成：修掉草稿的双 `#else`（内核构建全灭）、把 9 处共享区裸 `pr_info` 转到 `mt_gpu_vm_log`、补 8 项源码门禁。`check-offline`（394 Python + 299 C）、`make kernel`（`W=1` 零警告）、`make check`（L1+L2）全绿。会话未动（本轮开工时活会话已随冷启动消失，仅 `card0`，无模块在载）。
- **动机（实测）**：旧 `mt_mmu_bootstrap.h` 独立 userspace 编译（`cc -std=c11 -Wall -Wextra -Werror -O2 -shared -fPIC tests/mmu_bootstrap_wrapper.c`）失败——`kvzalloc` 隐式声明、`GFP_KERNEL` 未声明、`pr_info` 隐式声明；新头一次通过。各 harness TU 自带的拼写各异（`boot_bo_lifetime_test.c`、`system_memory_test.c`、`tqx_ram_io.h` 等），头文件自包含后不再依赖它们。

## 实测（执行过）

1. `scripts/verify-mmu-bootstrap.py` 全过（sparse/dummy/invalid 全场景），`reports/mmu-bootstrap-validation.json` **零 diff**——页表字节与 oracle 逐字节一致，重构零行为变化。
2. `make check` 在干净 HEAD 上已是红色（`gpu_vm_test.c` 止于 `mt_mmu_bootstrap.h:87 kvzalloc` 隐式声明）；本轮修完后 `CHECK-EXIT=0`。`make kernel` 单独复核 `KERNEL-EXIT=0`，`W=1` 零警告。
3. 新门禁 `tests/test_pvr_userspace_spellings.py` 8 项全绿；反向验证：把一处调用改回裸 `kvzalloc` / 裸 `pr_info` 即各抓 1 失败，还原即过。
4. `reports/runtime-integration-build.json` 随 `make check` 刷新后提交（含 `pvr_bridge_core_test` 272→299 的旧凭证更新 + 新 `module_sha256`；`shared-abi-baseline` 无漂移，宏替换未动布局）。

## 修掉的草稿缺陷（实测，非推断）

1. **双 `#else`**：草稿在 `mt_gpu_vm.h` 里留下 `#ifndef/#else/#endif` 完整块后再跟一个游离 `#else`，该 `#else` 被配到外层头卫宏，内核分支（含 `<linux/...>` 与 `zalloc` 定义）在内核构建中被整体跳过——`make kernel` 报 `mt_gpu_vm_zalloc` 隐式声明。已并回单一 `#ifndef/#else/#endif`。
2. **L2 半截**：bootstrap 自包含后，`make check` 下移到 `mt_gpu_vm.h` 共享区裸 `pr_info`（`gpu_vm_test.c` 不带 stub 编译）。6 处转 `mt_gpu_vm_log` 后，又下移到 `mt_process_resources.h`（1 处）与 `mt_boot_bo.h`（2 处），复用同一宏收敛（消息前缀保留，内核侧仍是 `pr_info`，行为不变）。

## 边界与下一步

1. `mt_boot_bo.h:118` 的 `kzalloc/GFP_KERNEL` 在 `gpu_vm_test` 链下可解析（L2 全过为证），未动——超出本轮范围，备忘。
2. 失败路径日志现在会进 userspace 测试 stderr（`mt_gpu_vm_log` → `fprintf`），连带写进 `runtime-integration-build.json` 的 checks 字符串；判定只看 exit code，无影响，诊断价值保留。
3. 本轮 `/tmp/opencode/r331/` 放过诊断日志（84K，已清空；`df /tmp` 仅 1%——未满但违规）。后续易失产物一律走 `mt-vgpu-guest/build/traces/<rNN>/`。
4. 下一刀（r328 已定义）：`[r8]` 出参槽读数——`core` 离线读或 GDB 断 abort 桩；需先重建活会话（另行批准）。
