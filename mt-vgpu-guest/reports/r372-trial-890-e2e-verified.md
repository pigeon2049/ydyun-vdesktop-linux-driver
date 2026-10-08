# r372：Trial 协议接受 `0x890==2`，端到端 TA 验证打通

## 结论

**端到端 TA 验证成功**——`mt_trial_start` 现接受固件预初始化状态（`0x890==2`），trial 成功建立（`pinned=1 connected=1`），真实 `0x82:0xC` TA kick 完整走通：dispatch → wire 分配 → `0x100` 完成 → fence signaled，无悬挂。

## Trial 协议研究

**`0x890` 寄存器语义**（guest 状态，由 guest 经 `mt_trial_guest_state` 写入）：
- `0`：无 session（fresh）
- `1`：连接中（`mt_fw_connect` 设置）
- `2`：活跃 session（`mt_fw_connect` 完成时设置）

**`0x898` 寄存器语义**（firmware 状态，只读）：
- `1`：firmware 就绪，等待 CONNECT
- `2`：firmware 活跃（trial 运行中）

**冷启动后 `0x890=2` 的含义**：固件/host 在启动时预置 guest 状态为 2，但 firmware 自身处于 state 1（就绪）。这不是残留，而是平台初始化行为。`mt_trial_start` 的上传步骤是幂等的（重写相同固件镜像并验证），因此跳过它不安全；正确做法是保留上传，仅放宽入口检查。

**`mt_fw_connect` 协议**（`mt_fw_connection.h`）：
1. `notify_online()` → `guest_state(1)`（0x890=1）
2. 发送 `MT_FW_CONNECT` 命令
3. 轮询至 `firmware_state()==2 && firmware_started()`
4. `guest_state(2)`（0x890=2），返回 0

当 `0x890` 入口已为 2 时，connect 仍会先写 1 再写 2，协议正常工作。

## 修复落点

**`mt-vgpu-guest/kernel/mt_fw_trial.h`**，`mt_trial_start`（+11/-3 行）：
- 入口检查：`readl(0x890) != 0` → 接受 `0` 或 `2`（`reg890 != 0 && reg890 != 2` → `-EBUSY`）
- 上传后二次检查：对比入口捕获的 `reg890` 值（而非硬编码 0），确保上传期间状态未变
- `0x898==1` 的 firmware 状态要求**未放宽**

**`mt-vgpu-guest/tests/test_trial_890.py`**（新增，4 tests）：
- 接受 `0x890==2`、拒绝其他值（1/3）、二次检查用入口值、firmware 状态要求保留

## 活体验证

**Probe 重载**（一次计划内）：rmmod bridge → rmmod probe → insmod 新 probe（含 trial 修复 + r371 的 0x890==2 接受）→ insmod bridge（r370 构建，未动）。

**Trial 建立**（dmesg）：
```
firmware trial: connect=0 disconnect=-61 restored=0 pinned=1 result=0
```
（此前为 `connect=-61 pinned=0 result=-16`）

**TA kick 端到端**（两次）：
```
musakickgfx2 dispatch: kick_ta=1 ... → submitted wire=1
musakickgfx2 dispatch: kick_ta=1 ... → submitted wire=2
```
- `pvr_session_acquire` 成功（无 ENODEV）
- `0x100` 完成到达（无 `pvr_ta_abandon` 超时警告）
- Bridge ref 归 0，无悬挂
- 两次运行一致（wire 递增）

## 门禁

- `make -C mt-vgpu-guest check-offline`：**421 Python + 299 C 全绿**（含新增 4 tests）
- `make kernel` W=1：**零警告**
- 反向验证：stash 修复 → 3/4 tests 变红；恢复 → 全绿

## Freeze 状态

- `mt_guest_probe`：重载一次（trial 修复），当前 ref 1（bridge 持有），绑定正常
- `mt_pvr_bridge`：r370 构建，未重建，仅随 probe 重载而 reload，ref 0
- dmesg：无新增 WARN/BUG/Oops
- `/dev/dri/card1` + `renderD128` 正常

## 诚实边界

- 本轮验证的是 marker 级 TA kick（零绘制），非真实渲染 payload
- `OUT.update_fence` 的 userspace 回填值为 0（dmesg 确认 wire 已分配，userspace 拷贝待查）
- `kick_pr=1` 仍 TO-VALIDATE（本轮仅测 TA-only）
