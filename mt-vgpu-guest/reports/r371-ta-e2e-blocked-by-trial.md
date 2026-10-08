# r371：端到端 TA 验证被固件 trial 状态阻塞（0x890=2 无法启动 trial）

## 结论

**端到端验证未完成**——真实 `0x82:0xC` TA-only kick 的 dispatch 已到达桥侧（dmesg 解码日志确认），但 `pvr_session_acquire` 返回 `-ENODEV`（19），因 `g->trial.pinned=0`（trial 未建立）。根因为固件状态问题，非 r370 代码所致：

1. **冷启动后 `0x890=2`**（固件启动即为 2，非残留；MMIO `0xfc610890` 实测）。
2. Probe 已修改源码接受 `0x890==2`（3 处：`mt_probe`、`mt_read_device_info`、`mt_reserve_memory`，`trial_connect=Y` 且 `!recover_channels` 时），成功绑定 00:0e.0（channels/info/memory 均 ok）。
3. **但 trial 无法启动**：`mt_trial_start`（`mt_fw_trial.h:216`）要求 `0x890==0 && 0x898==1`，当前 `0x890=2` 导致 `-EBUSY`；`mt_trial_run` 得 `connect_result=-61`（-ENODATA），`pinned=0`。
4. 固件状态不一致：MMIO `0x890=2`（guest 可见）vs `mt_trial_fw_state()`（读 `0x898`）`=1`。`mt_runtime_can_submit` 要求两者皆为 2，当前不满足。
5. Bridge 加载正常（r370 构建），`/dev/dri` 正常；dispatch 解码后因无 trial session 直接返 `-ENODEV`，未到达 `submit_ta_work`，r370 新增的 `pvr_ta_wait_complete` 未被执行。

**诚实边界**：r370 的生产 TA 完成路径（`pvr_ta_wait_complete` + `pvr_ta_abandon`）已实现并上机，但**从未被活体执行**（r370 被 `-EHOSTDOWN` 阻塞，r371 被 `-ENODEV` 阻塞）。其正确性目前仅由编译 + 门禁 + 反向验证保证。

## 详细过程

**环境恢复**（冷启动后）：
- Tailscale 恢复，SSH 正常。
- `0x890` 实测为 2（冷启动未清零）。
- 以最小参数（`enable_probe=Y`）试加载：绑定成功，但 `0x890` 未变化（firmware 未重置）。
- 确认 `mt_guest_probe.ko`（`kernel/`）为 r370 构建（`make kernel` 产物），源码未变。

**Probe 0x890 修复**（父 agent 执行，本轮入库）：
- `mt-vgpu-guest/kernel/mt_guest_probe.c` 3 处修改，`trial_connect=Y && !recover_channels` 时接受 `reg890==2`。
- 重载后绑定成功：`shared channel round-trip result=0`、`info response ok`、`memory reservation ok`、`transport bound`。
- 但 `firmware trial: connect=-61 disconnect=-61 pinned=0 result=-16`。

**TA kick 测试**：
- Harness：Python 经 `/dev/dri/renderD128`，`PVR_INIT(0x40046445, init_module=2)` → `PVR_BRIDGE(0xc0206440)` 发 `0x82:0xC`（268B IN，`kick_ta=1` at offset 188，其余 0；12B OUT）。
- INIT 返回 0。
- BRIDGE 返回 `errno=19`（ENODEV）。
- dmesg 确认 dispatch 到达：`musakickgfx2 dispatch: kick_ta=1 kick_3d=0 kick_pr=0 ...`。
- 无后续日志（`pvr_session_acquire` 返 NULL，直接 `-ENODEV`）。

**根因分析**：
- `pvr_session_acquire`（`mt_pvr_bridge.c:674`）：`if (!g->trial.pinned || !g->trial.connected)` → NULL → `-ENODEV`。
- Trial 状态（sysfs）：`pinned=0 connected=0 connect_result=-61`，`guest=2 firmware=1`。
- `mt_trial_start`（`mt_fw_trial.h:216`）：`readl(0x890) != 0` → `-EBUSY`，trial 无法启动。
- 固件 MMIO `0x890` 与 RPC 状态不一致，guest 侧无法重置（firmware-controlled，probe 只读）。

## 文件

- Probe 修复：`mt-vgpu-guest/kernel/mt_guest_probe.c`（3 处，见 diff）
- 测试 harness：`mt-vgpu-guest/build/traces/r371/ta_kick_e2e.py`（0600，未入库）
- 报告：本文件 `mt-vgpu-guest/reports/r371-ta-e2e-blocked-by-trial.md`
- 证据（0600）：`mt-vgpu-guest/reports/r371-dmesg.txt`（dispatch 到达 + ENODEV）

## 门禁

- `make -C mt-vgpu-guest check-offline`：待跑
- `make kernel` W=1：probe 修改需验证零警告

## Freeze 状态

- `mt_guest_probe`：重载一次（0x890 修复），当前 ref 0，绑定正常
- `mt_pvr_bridge`：r370 构建在载，未重载，ref 0
- dmesg：除测试日志外，无新增 WARN/BUG/Oops
- `/dev/dri/card1` + `renderD128` 正常

## 建议的下一步

固件 `0x890` 状态需 host 侧或固件级别重置（guest 无法写入）。或研究 trial 协议是否有 "attach to existing session" 路径（当前只有 start/restore，`0x890=2` 时两者皆不可用）。在 trial 恢复前，端到端 TA 验证无法进行。
