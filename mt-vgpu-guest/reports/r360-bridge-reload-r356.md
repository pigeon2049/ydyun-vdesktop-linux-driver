# r360：`mt_pvr_bridge` 重载至 r356 构建成功——新桥 build-id `0d6b…55da`，活体 connect 健康检查 PASS，freeze 已恢复（真机活体，用户已批准 2026-10-08 15:13）

## 结论（实测）

1. **预检**（15:14）：`fuser /dev/dri/renderD128` 无持有者；`mt_pvr_bridge` ref 0；`mt_guest_probe` ref 1（全程不碰）；在盘构建 1739856B（14:34）、vermagic `6.12.111+deb13-amd64` 与 `uname -r` 一致、含 observer 串 `musakickgfx2 observe`（grep=1）。
2. **`rmmod mt_pvr_bridge` 成功**；`/proc/modules` 确认移除；`/dev/dri` 剩 card0（card1/renderD128 随桥消失，符合预期）。
3. **`insmod …/kernel/recovery/mt_pvr_bridge.ko` 成功**；新桥 build-id `0d6bb8d74929eec7c0bc1f88ec0b4dae7bd455da` == 在盘 r356 构建 ≠ 旧 `2a2af0a4…`。
4. **dmesg**：`mt_pvr_bridge: unloaded cleanly` → `[drm] Initialized pvr 0.1.0 for 0000:00:0e.0 on minor 1` → `registered 'pvr' node, bridge stage 1: main module still owns the device (no binding)`；无新增 WARN/BUG/Oops。
5. **健康检查 PASS**（复用 r358 方法，纯 userspace）：`PVRSRVConnectionCreateDevice`→0、`GetSrvHandle` 指针形态、单次 `PVRSRVBridgeCall(1,0)`→0 且 OUT 逐字节命中（`bvnc=0x0023000406600017`/`error=0`）。
6. **freeze 恢复确认**：probe 仍绑定 00:0e.0（ref 1，未碰）；bridge 默认参数在载（ref 0）；card0/card1/renderD128 存在；dmesg 干净。

## 方法教训（实测）

- **load bias 计算**：`/proc/self/maps` 取 bias 必须减去 segment 的 file offset（`bias = addr - offset`）。本轮首跑误取 r-xp 映射地址为 bias（高 0x2c000），`PVRSRVBridgeCall` 跳错地址致 userspace segfault 一次（dmesg 有记录；纯 userspace，桥未受影响）。修正后 `dlsym(GetSrvHandle) == bias+0x3c1c0` 精确成立。
- **`PVRSRVBridgeCall`（0x92930）真签名为 7 参数**：`(handle, bridge, func, in_ptr, in_len, out_ptr, out_len)`（rdi,rsi,rdx,rcx,r8,r9,stack）；桥侧 ioctl struct 32B = `{u32,u32,ptr,ptr,u32,u32}`（`_IOWR('d',64,32)`）。5 参数调用会把 out_ptr 取错（r9 错位）→ 走 ENOTTY 分支返回 `0x26`。Connect 调用实证：r8d=`0x10`（IN 16B）、stack=`0x11`（OUT 17B）。

## 边界与未做事项（live 边界，如实）

- 只换桥 + 建连验证：未发 `0x82:0xC`，未提交任何 GPU 工作。
- 未跑 `make probe`（L3）：其 WITH_BRIDGE 会 rmmod 桥；以"open + INIT + connect 全交互正确 + OUT 逐字节命中"为等效健康证据（沿用 r358 取舍，如实记录）。
- 远端 /tmp 曾误写一份 dmesg 快照（命令拼写失误），已即刻删除；其余中转走 scratch/build/traces。

## 门禁

- `make -C mt-vgpu-guest check-offline`：400 Python + 299 C 全绿。
- 本轮无代码改动（纯 live 操作 + 文档），无新增门禁测试需求。

## 证据（0600）

- `reports/r360-precheck.txt`：预检快照（holders/refcnt/vermagic/observer 串）。
- `reports/r360-dmesg-diff.txt`：换桥前后 dmesg 差异。
- `reports/r360-health-check.txt`：harness 健康检查全文输出（含 PASS）。
- `reports/r360-new-buildid.txt`：新旧 build-id 对照。
