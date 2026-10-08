# r359：在载桥早于 r356、无 0x82:0xC observer——活体 IN 参数观察暂停，待重载窗口（真机，安全协议停轮）

## 结论（实测）

1. **基线（跑前，freeze 未碰）**：`mt_pvr_bridge` ref 0 / `mt_guest_probe` ref 1；`/dev/dri` 有 `card0`/`card1`/`renderD128`；dmesg 尾部为 `arena close` 行，无 WARN/BUG/Oops；UMD `.so` SHA `b3058c02…34237b0` 对版；HEAD `12c6bd6`（r358），工作区干净。
2. **在载桥版本核查（任务 §2 硬性门，未通过）**：
   - 在载桥 build-id：`2a2af0a4daf114c8ca0b336fea323d2bf02d261f`（`/sys/module/mt_pvr_bridge/notes/.note.gnu.build-id` 实测）。
   - 在盘 r356 构建 `mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.ko`：build-id `0d6bb8d74929eec7c0bc1f88ec0b4dae7bd455da`，mtime 14:34:49，含 observer 串 `musakickgfx2 observe`（grep=1）。
   - 在载桥最后一次加载在 dmesg `[5635.62]`（`registered 'pvr' node`），按 uptime 折算约 11:26；**早于 r356 提交（14:37:27）约 3 小时**。r356 提交信息注明 "Offline, zero hardware touch, freeze intact"，r357/r358 亦未重载桥。
   - 结论：**在载桥不含 `pvr_cmd_musakickgfx2_observe`**。pre-r356 dispatch 对 `0x82:0xC` 只走 `default: return -ENOTTY`（无解码日志，源码实证）。
3. **按任务安全协议 §2 停轮**：未发 `0x82:0xC`（无 observer 时发包只会拿到裸 `-ENOTTY`，达不到 IN 参数观察目标）；未重载桥（换桥需用户协调窗口，r280/r281 教训）；freeze 会话全程未碰（无 rmmod/insmod/unbind/参数修改）。
4. **门禁**：`make -C mt-vgpu-guest check-offline` 400 Python + 299 C 全绿；本轮无代码改动。

## 版本证据（实测，见 `r359-bridge-version.txt`）

- 在载 build-id vs 在盘 build-id（`readelf -n` vs `/sys/.../notes`）：`2a2a…261f` ≠ `0d6b…55da`。
- 在载桥加载时间：dmesg `[5635.62]` → ~11:26；r356 commit `8a0b32f` 14:37:27。
- pre-r356 `pvr_bridge_dispatch` default 分支源码：`default: return -ENOTTY;`（`git show 8a0b32f^` 实证）。

## 边界与未做事项（live 边界，如实）

- 未发 `0x82:0xC`，未跑 TA 路径；IN 参数观察未执行（缺 observer，前置不满足）。
- 未跑 `make probe`（L3）：其 `WITH_BRIDGE` 会 rmmod 桥，违反红线 §1；本轮桥未被扰动。
- 收尾：dmesg 无新增 WARN/BUG/Oops；refs（bridge 0/probe 1）不变；`/dev/dri` 不变。

## 下一步（r360 建议）

1. **待用户协调重载窗口**（参考 r216 流程：装盘前验 `strings`+`vermagic`，单桥重载，probe 不碰，重载后 L3 全绿即恢复 freeze）：将在盘 r356 构建（含 observer）上机。
2. 重载后重跑 r359 原计划：真实建连（复用 r358 harness）+ fabricated `RGXKickTA`（r353 驱动，b10 描述子已 poke，GDB 开 ASLR）→ TA 路径发出 `0x82:0xC` → 桥侧 observer 解码日志 → 与 r356 `mt_pvr_wire.h` 268B 结构逐字段对照。
3. 不重载则无替代路径：fabricated 侧 r353/r354 已做完；活体 IN 观察必须有 observer。

## 门禁

- `make -C mt-vgpu-guest check-offline`：400 Python + 299 C 全绿。
- 本轮无代码改动，无新增门禁测试需求。

## 证据（0600）

- `reports/r359-bridge-version.txt`：在载/在盘 build-id、加载时间折算、r356 提交时间、dispatch default 源码实证、基线快照。
