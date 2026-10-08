# r367: 0x82:0xC 真实分发接 submit_ta_work —— 分发活体验证通过，fence 语义受阻于 UAF bug（已修源码，未上机）

**结论**：`0x82:0xC`（MUSAKickGFX2）的桥侧 dispatch 已从 r356 的 observer（`-ENOTTY`）改为真实分发，
经 `mt_bridge_submit_ta_work` 走 DM3。活体验证：TA+PR kick 被诚实拒绝（`-EOPNOTSUPP`，D5），
TA-only kick 真实执行（返回 0，`OUT.update_fence=6`）。但 fence 语义验证（T2/T3）受阻——
首个 TA marker（wire 6）未在 5s 内完成，且发现 dispatch 在成功路径上 `kfree(ctx)` 造成
use-after-free（op 把 ctx 存入 `m->context`，completion 路径会解引用）。
UAF 已在源码中修复（不再 live kfree），门禁全绿；但 wire 6 仍 pending 导致桥 refcnt=1，
无法进行第二次重载上机修复版。r368 需解决 stuck marker 后重载验证。

## 实现

**落点**：`kernel/recovery/mt_pvr_bridge.c` `pvr_cmd_musakickgfx2()`（替换 `pvr_cmd_musakickgfx2_observe`），
dispatch case 更新为 `/* MUSAKickGFX2 (real TA dispatch, r367) */`。

**映射**：`kernel/mt_ta_submit.h` `mt_ta_params_from_musakickgfx2()`（IN→`mt_ta_submit_params` 纯函数，D5）：
`ta_cmd_va←p_ta_cmd`，`ta_cmd_size←ta_cmd_size`，`kick_flags←kick_ta/kick_pr` 位，
`ta_upd_*`/`ta_fence_*` 三元组 + 计数，`pr_fence_*` 三元组，`check_fence←check_fence`。

**分发逻辑**：
1. `pvr_in` 解码（保留 r356 解码日志）+ D8（userspace 指针解码时捕获）。
2. 预检：`!kick_ta || kick_3d` → `-EOPNOTSUPP`（PR/3D 本轮无执行路径）。
3. `pvr_session_acquire` 取 `mt_guest`（只读，不碰 probe）；`trial_lock` 下检查
   `s->lock`/`can_submit`/`opaque`/`ready`/`work_ready`。
4. 分配 ctx（`route.dm=MT_FW_DM_TA`），`work.job.state=MT_JOB_HELD`，映射填 params；
   `kick_pr` 置位则记录 TO-VALIDATE（不编造 PR 语义）。
5. `s->ready=true`；**解锁 trial_lock**；调 `mt_bridge_submit_ta_work`
  （op 内部管理 `s->lock`，调用者绝不能持有）；重加锁后 `s->ready=false`。
6. 成功：`OUT={error=0, update_fence=wire_id, update_fence_3d=0}`；失败：`kfree(ctx)` 直接返 errno。
7. 锁序：dispatch 已持 `file->lock`，只取 `trial_lock`（translator 已有先例）。

**UAF 修复**（活体中发现，源码已修，未上机）：
成功路径上原先 `kfree(ctx)`，但 op 已将 `work->context` 转入 `m->context`，
`mt_marker_complete_ta` 在固件完成时会解引用 `m->context->active_jobs`。
r366 verifier 因在 completion 后才释放而未触发；dispatch 在 ioctl 返回前释放即 UAF。
修复：成功路径不再 `kfree`（ownership 转交 marker；op 在 completion 时 `m->context=NULL`
但不释放——R5 需补释放，目前为有界泄漏，已注释）。

## 活体验证（2026-10-08，新桥 build-id `4a78b331…`）

**方法**：Python harness 经 `/dev/dri/renderD128` 直接发 ioctl。
先调 `DRM_IOCTL_PVR_INIT`（`0x40046445`，`init_module=2`）建连（否则 dispatch 返 `-ENOTCONN`），
再发 `0x82:0xC`（`0xc0206440`，268B IN / 12B OUT）。零绘制 kick，不提交真实 TA payload。

| 测试 | IN | 期望 | 实测 | 结论 |
|------|-----|------|------|------|
| A | TA+PR（r363 值：`kick_ta=1,kick_pr=1,ta_upd=1`） | `-EOPNOTSUPP` (95) | errno=95，dmesg 见 `kick_pr=1 TO-VALIDATE` + `submit_ta_work -> -95` | ✅ 通过 |
| B | TA-only（`kick_pr=0,ta_upd=0,check_fence=0`） | 0，`update_fence>0` | errno=0，`OUT={0, wire=6, 0}`，dmesg `submitted wire=6` | ✅ 通过 |
| C | TA-only，`check_fence=6`（T2） | 0（依赖已满足） | errno=110（`-ETIMEDOUT`），5s 等待超时 | ❌ 受阻 |
| D | TA-only，`check_fence=0x7fffffff`（T3） | `-EINVAL` (22) | errno=112（`-EHOSTDOWN`，transient，r366 亦见） | ❌ 受阻 |

**DM3 接受**：✅（B 返回 0，wire=6 分配，`mt_fw_queue_try_submit` 成功）。
**完成码 0x100**：⚠️ 未 live 验证（wire 6 未完成；op 与 r366 字节相同，r366 已证 0x100）。
**fence 语义**：⚠️ 未 live 验证（C/D 受阻）。

## 受阻根因

1. **UAF bug**（已定位，源码已修）：dispatch 成功路径 `kfree(ctx)`，而 `m->context` 仍指向它。
   若 wire 6 完成，将触发 UAF。修复版源码已就绪，但未上机。
2. **wire 6 未完成**（原因未明）：自提交后 90s+ 未见固件完成事件。可能原因：
   firmware 未处理该 marker、completion event 丢失、或 probe event 路径问题。
   r366 的 markers（wire 1-5）均正常完成，op 代码相同。
3. **无法重载验证修复版**：wire 6 pending 导致 `mt_pvr_bridge` refcnt=1（marker pin bridge，
   by design），`rmmod` 会因 "in use" 失败。本轮已用掉一次计划重载。

## 当前状态（freeze）

- `mt_guest_probe`：ref 1，未动 ✅
- `mt_pvr_bridge`：ref 1（wire 6 pending pinning），无 WARN/BUG/Oops ✅
- dmesg：仅本轮测试日志，无新增异常 ✅
- 源码：UAF 修复已入库，门禁全绿 ✅
- Live 桥：仍为 UAF 版本（含 bug），wire 6 悬挂 ⚠️

## 门禁

- `make -C mt-vgpu-guest check-offline`：408 Python tests OK + 299 C checks OK ✅
- `make kernel` W=1：零警告、零错误 ✅
- 新增 `tests/test_ta_kick_dispatch.py`：5 tests，验证 IN→params 映射（r363 值 + TA-only 变体）✅
- 反向验证：篡改映射（`kick_ta`→`kick_3d`）→ 2 failures；恢复后全绿 ✅

## 诚实边界

- Live 验证仅覆盖 dispatch→op→DM3 提交；未验证 fence 完成语义（C/D 受阻）。
- 完成码 0x100 未在本轮 live 验证，引用 r366 结论（op 未改）。
- 未提交真实 TA 渲染 payload（零绘制 kick）。
- `kick_pr=1` 仍为 TO-VALIDATE（诚实拒绝，不编造）。
- ctx 在成功路径上有界泄漏（R5 需在 op completion 路径补释放）。
- Live 桥含 UAF bug；wire 6 悬挂 pinning 桥（refcnt=1）。

## r368 建议

1. **解决 stuck wire 6**：调查 firmware 未完成 marker 的原因（event 路径？queue 状态？）。
   可能需 probe 侧诊断或等待 firmware 超时清理。
2. **重载上机修复版**：wire 6 清理后（完成或确认永不完成），重载含 UAF 修复的桥。
3. **补全 C/D 验证**：T2（`check_fence=W` 已完成→0）、T3（非法 id→`-EINVAL`）。
4. **R5**：补 `m->context` 在 completion 路径的释放；`vm->sealed` 校验门。

## 文件

- 实现：`kernel/mt_ta_submit.h`（映射函数），`kernel/recovery/mt_pvr_bridge.c`（分发+UAF修复）
- 门禁：`mt-vgpu-guest/tests/test_ta_kick_dispatch.py`
- 报告：本文件 `mt-vgpu-guest/reports/r367-ta-kick-dispatch-live.md`
