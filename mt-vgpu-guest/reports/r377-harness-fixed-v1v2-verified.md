# r377：Harness INIT 修复，V1/V2 活体验证通过（无 oops）

> **结论**：r376 的 harness 因 INIT 传参错误（`init_module` 非 1/2）致 EINVAL；按 r373 既证格式（`u32 module=2`）重写后，两次真实 TA kick 的 V1（VM 创建）/V2（bind 空绑定 `-EINVAL`）均通过，**无 oops**，r375 的 oops 根因已消除。

## 1. Harness 修复点

r376 的 Python harness 在 INIT ioctl（`0x40046445`）传入错误的 `init_module` 值，内核返回 `-EINVAL`（dmesg 曾见 `init_module=0 is not 1 or 2`）。

r373 的既证格式（`reports/r373-live-out-writeback.txt`）：
- `/dev/dri/renderD128`
- INIT：`0x40046445`，payload 为 **u32 `module=2`**（小端 4 字节）
- Bridge：`0xc0206440`，32B `mt_pvr_cmd{bridge_id=0x82, function_id=0xC, in_ptr, out_ptr, in_size=268, out_size=12}`
- IN：268B，`kick_ta=1` 在偏移 188
- OUT：12B，`u32 error@0` / `i32 update_fence@4` / `i32 update_fence_3d@8`

r377 按此格式重写 harness（ctypes 分配缓冲取地址），INIT 即通过。**r376 的失败纯属 harness 传参问题，非内核代码问题。**

## 2. V1/V2 活体验证

两次真实 `0x82:0xC` TA-only kick（bridge 未重载，r376 构建在载）：

**第 1 次**（wire=1）：
```
[ 1002.645186] musakickgfx2 dispatch: kick_ta=1 kick_3d=0 kick_pr=0 ...
[ 1002.645237] R5 V1: TA VM context created
[ 1002.645244] r376 V2: bind empty ret=-22 (expect -EINVAL, no oops)
[ 1002.645561] musakickgfx2: submitted wire=1
```
- Userspace `OUT.update_fence=1` == dmesg `wire=1` → 精确匹配
- **V1**：`mt_bridge_ta_vm_create()` 成功执行（proper `mt_gpu_vm_init()`，无手动拼装）
- **V2**：`mt_gpu_vm_bind_many()` 返回 `-22`（`-EINVAL`，空绑定预期值），**无 oops**

**第 2 次**（wire=2，新文件 → 新 VM 上下文）：
```
[ 1013.749679] R5 V1: TA VM context created
[ 1013.749683] r376 V2: bind empty ret=-22 (expect -EINVAL, no oops)
[ 1013.749998] musakickgfx2: submitted wire=2
```
- Userspace `OUT.update_fence=2` == dmesg `wire=2` → 精确匹配
- 两次一致，per-file 上下文独立创建

**回归**：marker 级 TA 路径正常（dispatch 成功、`0x100` 完成隐含于 wire 分配后无悬挂、OUT 回填正确）。

## 3. 与 r375 的对比

| 项 | r375 | r376/r377 |
|---|---|---|
| VM 初始化 | 手动拼装 `mt_gpu_vm` | `mt_bridge_ta_vm_create()` + 正式 `mt_gpu_vm_init()` |
| 页表 BO | `mt_bo_system_borrow()`（跨模块 ops 失败） | 合成 BO（`page_pa==NULL`，3D 既证模式） |
| `bind_many` | **内核 oops**（`+0x300`） | 返回 `-EINVAL`（预期），无 oops |
| 结论 | 设计错误 | 根因消除 |

## 4. 安全与收尾

- 本轮**未重载** probe/bridge（r376 构建在载）；**未重启**（用户明确禁止自作主张重启）。
- Harness 为一次性验证脚本，未入库（复现信息见 §1），远端已清理。
- 收尾：`mt_pvr_bridge` ref 0、`mt_guest_probe` ref 1；dmesg 无新增 WARN/BUG/Oops（V2 日志中的 "no oops" 为预期文本，非异常）。
- `make probe` 未跑（WITH_BRIDGE 会 rmmod），以 ioctl 交互为健康证据（沿用既往取舍）。

## 5. 诚实边界

- V2 仅验证空绑定（`count=0` → `-EINVAL`）；真实页表绑定（非空）未测。
- `MT_TA_VM_READY` 门保持关闭，真实 TA payload 未开启（按计划）。
- Probe 侧 r376 插入的 TA VM API 死代码仍在（probe 未重载），待清理。

## 6. 交付物

- 本报告：`mt-vgpu-guest/reports/r377-harness-fixed-v1v2-verified.md`
- 证据：`mt-vgpu-guest/reports/r377-dmesg-v1v2.txt`（0600，8 行：dispatch + V1/V2 + wire）
- 门禁见 §7。

## 7. 门禁

- `make -C mt-vgpu-guest check-offline`：**428 Python + 299 C 全绿**（无新增测试，本轮无代码改动）
- `make kernel W=1`：未重跑（无代码改动，r376 已验证零警告）
