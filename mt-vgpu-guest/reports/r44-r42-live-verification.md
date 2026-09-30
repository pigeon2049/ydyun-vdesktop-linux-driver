# r44：r42 新布局真机验证（主模块重载后）与卸载期新发现

日期：2026-09-30（重启后会话）。关闭 `r42-vm-mapping-scale.md` 第 6 节的两个真机遗留项。

## 1. 序列（可复现）

1. 重启后本机 `sudo` 可用（`NoNewPrivs=0`；上会话容器内不可用）。
   `mt_guest_probe` 未加载，无需销毁旧会话（旧会话已随重启消失）。
2. `echo 0000:00:0e.0 > /sys/bus/pci/drivers/mtgpu/unbind`：
   官方 `mtgpu` 驱动在启动时即 `PhysHeapsInit` 失败（`-19`，无设备节点，
   引用计数 0），显示走 QXL（`card0` = `00:02.0`），解绑不影响桌面。
3. 只读 `mt-status --read-status`：`driver_state=2, firmware_state=1`
  （unbound 但 Guest 仍为 2，重启后宿主侧残留；30 秒 3 次轮询稳定）。
   `fresh-trial.py` 预检因此判 `eligible=false`（要求 Guest=0/FW=1），
   未绕过：主模块 probe 自身同样要求 `regs+0x890 == 0`（`mt_guest_probe.c`
   第 319/330/364/398 行），直接加载只会被安全拒绝。
4. `insmod kernel/recovery/mt_cold_disconnect.ko`（`finish=0` 只读）：
   6 DM 全环 `head == tail`、`started=0`、`FW=1`，`result=0`。
   注：`fw_pa=0x779fef000`（非旧 `0x771fef000`，宿主重启后重分配；
   helper 正确回退到 `fw_offset=0x200000`）。
5. `insmod ... finish=1`：`guest=0 firmware=1 started=0`（模块回读 +
   mt-status 双重确认），`rmmod` helper。
6. `fresh-trial.py --run --runtime-context`：`insmod rc=0`、
   `connected=1`、`published=1`，
   `guest=2 firmware=2 started=1 pinned=1 retained=1`
  （`disconnect_result=-61` 为保留式会话的预期值，未尝试断开）。
   主模块为本次重编的新布局（`module_sha256` 与 `runtime-integration-build.json` 一致）。
   证据：`build/fresh-trials/20260930T052647Z-28c45d79/`（gitignored，不入库）。

## 2. r42 验证结果（全部通过）

`insmod kernel/recovery/mt_live_3d_drm.ko` **不再 `-EBUSY`**（r42 阻塞消除，
主/恢复模块布局一致）：

```text
mt_live_3d_drm: prepared unified 2D VM (pages=18, maps=20/15872) & 3D VM (pages=21, maps=23/15872)
[drm] Initialized mtvgpu 0.3.0 for 0000:00:0e.0 on minor 1
```

`mt-3d-check 20`（含 r42 新增断言 `vm3d_max_mappings > 24`）：

```text
[*] GPU VA mappings: 2D=20  3D=23/15872 (page-budget derived)
[*] Submitting 20 3D frames ... Frame 1..20 [OK]（首帧 103us，其余 ~60us）
[*] Completed: submitted=20 completed=20 last_sequence=20
[*] Testing 3D Render Target binding & VRAM readback...
    Render Target 3D Frame executed: seq=21 [OK]
    Successfully read back Render Target VRAM (64 KiB) after GPU execution [OK]
=== Test Passed Successfully ===
```

- `3D=23/15872`：23 个映射（11 上下文 + 1 命令 + 9 私有/boot + 2 render slots，
  与 r41 一致），上限 `15872 = min(31×512, 16384)`（32 页预算）正好是
  r42 推导值在真机上的首次实测。
- r41 的 20 帧基线（`seq` 另一轮）在新布局下完整复现，外加 render-target 读回。

## 3. 新发现：sealed 恢复空间卸载报 WARN（预先存在，与 r42 无关）

`rmmod mt_live_3d_drm` 完成（`unloaded cleanly`），但 dmesg 留两条 WARNING，
均为 `release_unpublished` 内 `destroy()` 返回 `-EBUSY`（`RAX=0xfffffff0`）：

- `+0xe8`：第 674 行 `destroy(space_3d)`；
- `+0x12d`：第 679 行 `destroy(space_2d)`。

根因：`prepare_context` 对两个空间都执行了 `seal`
（第 747/850 行），而 `mt_gpu_vm_fini` 拒绝已 seal 的 VM（`-EBUSY`）——
该拒绝在 r42 前后完全相同（`ee1dd8d^` 一致），且 `mt_gpu_vm.h` 注释明示
“sealed VM 在真正的 context-withdrawal/TLB 协议实现前不能释放”。
r41 从未在 seal 后卸载该模块，故此前无人触发。

泄漏记账（代码路径 + 实测吻合）：

- `destroy` 失败直接返回，`mt_vm_vram` 对象不 `kfree`（每周期漏 2 个）；
- 所绑 BO 永不释放；每个 VRAM backing 在分配时做过
  `__module_get(THIS_MODULE)`（`mt_bo_vram.h:50`），只在 `mt_bo_vram_free:82`
  才 `module_put`——实测主模块引用计数每加/卸载周期 **+26**（36 → 62），
  且 `store->objects` / `allocated_bytes` 永不回落。

复现性：第二次 `insmod`（节点顺延为 `renderD129`/`card2`，
`mt-3d-check` 硬编码 `renderD128`/`card1`，用临时 symlink 覆盖验证）+
3 帧 + render-target 读回（`seq=22..25` 连续）全部通过，
证明会话在 WARN 卸载后依然健康；第二次 `rmmod` 报出同样的两条 WARN。

## 4. 运行规则（与仓库既有结论一致）

- 已 seal 的恢复模块是长寿的，**不要热卸载**
  （r32/r34“不能热卸载”规则同样适用于本模块）。
  WARN 是泄漏的诚实信号，不要静默 `-EBUSY`。
- 要释放 sealed 空间，需要先实现 context-withdrawal/TLB 协议——
  这是独立工作项，不在本阶段范围内，本次不改代码。
- 当前终态（符合重启前常规）：新布局主模块已加载并保留会话
  （`guest=2 firmware=2 started=1 event_result=0`，`pending=0 completed=25`），
  恢复模块已卸载；`00:0e.0` 归自研驱动，官方 `mtgpu` 保持解绑
  （它启动时即初始化失败，无功能损失）。
- 内核 taint 新增 `W`（WARN）位；之前为 `OE`。

## 5. r42 收尾状态

- ✅ 新布局主模块真机加载 + 512-mapping 前提（上限 15872 实测）；
- ✅ QUERY `vm3d_max_mappings` 真机值（15872）；
- ✅ r43 已离线关闭重叠检测 errno 等价性；
- ⏳ 三角形/渲染路径选型（PVRSRV 桥接 vs 逆向 PSC）仍待用户决定。
