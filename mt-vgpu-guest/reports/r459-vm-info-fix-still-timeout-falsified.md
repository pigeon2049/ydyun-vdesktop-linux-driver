# r459：TA 包 VM 信息修复活体仍 5s 超时——MMU fault 假说被证伪

**结论：r458 的 TA 包 VM 信息修复（+0x18 root_pa、+0x20 token）活体仍 5s 超时。r457 的"MMU fault"假说被活体证伪。Trial 重建成功，13 BO 全部绑定，render context READY，但固件无响应。Bridge ref=1（pending TA fence），按协议停止。**

## 系统确认（[MEASURED]）

| 检查项 | 结果 |
|---|---|
| 第 17 次冷重启 | ✅ uptime 0 min（23:29） |
| 初始模块 | ✅ `mt_guest_probe`/`mt_pvr_bridge` 均未加载 |
| HEAD | ✅ `95ce2a7`（r458，已 push），工作区干净 |

## Trial 前阻塞修复

**审计 JSON 过期**（r451 同模式）：`reports/runtime-integration-build.json` 的 module SHA 为 r451 时的值（`f188b9f2...`），当前模块 SHA 为 `d5f44086...`。
- `mt_guest` pahole 输出与 backup 完全一致 ✅
- 7 个 ABI 结构体与 baseline 无漂移 ✅
- 已更新 `module_sha256`（保留此改进，如 r451）

**Trial 结果**：
```
shared channel round-trip result=0 mode=1 registered=15
firmware trial: connect=0 disconnect=-61 restored=0 pinned=1 result=0
experimental Guest transport bound; trial_bus_master=1
```
✅ Trial 重建成功。

## 双门控构建

- `MT_TA_REAL_PACKET=1` + `MT_TA_READBACK_DEBUG=1`（static_assert 临时中和）
- userspace `n_entries` 1→0（Header-only，r423）
- `make kernel` W=1 **零警告**
- T1-T5：607/609 通过（2 个 gate_default_off 为 dual-gate 期间预期失败）；C 全绿
- 构建后已 revert，工作区干净（仅保留 audit JSON 更新）

## 活体结果：5s 超时 ❌

`./build/userspace/mt-ta-readback /dev/dri/card1 build/traces/r459/pixels-r459.bin`：
```
[*] connected bvnc=0x23000406600017
[*] render context handle=0x1000
check failed line 161: 0 errno=110
```

dmesg 确认：
- 13 个 BO 全部绑定（BO 0-10，target 0x7b000000，rgnheader 0x7c000000 bytes=4096）
- `r397: exec process/contexts created (3D node_type=5, TA node_type=2)`
- `render context READY (11 BOs, CSW, exec)`
- **固件无响应**

**r457 假说被证伪**：TA 包已携带 `+0x18` root_pa 和 `+0x20` token（r458 实现），但行为**无任何变化**——仍为 5s 超时（errno=110），而非完成或新错误码。

## 假说证伪链（r453–r459）

| 轮次 | 假说 | 结果 |
|---|---|---|
| r453 | RgnHeader 未填充真实 region 数据 | 被 r454 证伪（1s 即完整初始化） |
| r454 | RgnHeader 初始化量不足（一半） | 被 r456 活体证伪（128 dwords 仍超时） |
| r457 | TA 包缺失 VM 信息 → MMU fault → hang | **被 r459 活体证伪**（修复后行为无变化） |
| r459 | — | **Header/包内容方向已穷尽** |

## 安全

- Bridge refcount 0→1（pending TA fence），r440/r451/r456 同模式；按协议停止，未尝试卸载
- `mt_guest_probe` ref=1（retained trial）
- 双门控源码已 revert；工作区干净（仅保留 audit JSON 更新）
- 未用 `rmmod -f`；未自行重启；dmesg 零 WARN/BUG/Oops
- 证据：`mt-vgpu-guest/build/traces/r459/dmesg-r459.txt`（0600）

## 门禁（生产代码）

- `make -C mt-vgpu-guest check-offline`：**609 Python OK**（1 skipped）+ **pvr_bridge_core_test OK (851 checks)**
- `make -C mt-vgpu-guest kernel W=1`：**零警告**
- 无生产代码变更（本轮纯验证 + audit JSON 更新）

## 诚实边界

- [MEASURED]：VM 信息修复活体仍 5s 超时；13 BO 绑定；context READY；行为与 r451/r456 完全一致
- [FALSIFIED]：r457"MMU fault"假说
- [UNKNOWN]：固件超时的真实根因
- 本轮零硬件触碰（除 trial/活体必需的 insmod 外）；**未链入下一轮**

## 下一步（需 parent/用户决策）

1. **P0**：用户第 18 次冷重启（bridge ref=1 pending）
2. **P1**：Header/包内容方向已穷尽。剩余候选：
   - Bridge 参数缺失（`RGXKickTA3DKM` 约 40 参数 vs 我方最小集）——r457 [INFERRED 中]
   - 固件 TA 命令解析的其他前置条件（host proxy 行为）
   - 考虑从 UMD 侧抓取真实 TA 提交的完整包/参数进行对比
3. 不建议继续在包字段上盲试——r453–r459 已穷尽包内容方向
