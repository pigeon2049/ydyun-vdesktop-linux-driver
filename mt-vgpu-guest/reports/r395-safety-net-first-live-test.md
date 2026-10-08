# r395：安全网首个实战检验——pre-live 门禁全绿 + TA 回归双 kick 通过（活体）

> **结论**：r394 的三类安全测试（T1/T2/T3）在本轮首次作为 pre-live 门禁执行：check-offline 472+299 全绿（含 9 个新安全测试），scripts/safe_rmmod.sh 存在且可执行（0755）。随后在安全网下做 TA 回归：两次真实 0x82:0xC TA-only kick（r377 既证 harness 模式），OUT.update_fence=4/5 与 dmesg wire=4/5 精确匹配，零 "completion timeout"（0x100 完成到达），dmesg 零 WARN/BUG/Oops，refs 不变（bridge 0 / probe 13 基线）。本轮零模块操作（未重载、未卸载），rmmod -f 零出现。

## 1. Pre-live 安全门禁（离线执行）

| 项 | 结果 |
|---|---|
| make -C mt-vgpu-guest check-offline | 472 Python + 299 C 全绿（含 r394 新增 9 测试：T1x2 VM 完整性、T2x4 opcode 白名单、T3x3 卸载安全） |
| scripts/safe_rmmod.sh | 存在，0755 可执行；refcount 查验（0→rmmod；持有者名→exit 3 拒绝；数字非 0→exit 4 拒绝） |
| T1（VM 完整性） | 通过——源码零 ranges/page_lists 手动赋值（r376 后已无拼装） |
| T2（opcode 白名单） | 通过——(3,0x66) TA 钉死 DM3；(2,0x66) 永禁；本轮 harness 只用 (3,0x66) |
| T3（卸载安全） | 通过——仓库零 rmmod -f；本轮未执行任何卸载 |

Pre-live 清单核对（r394 报告第 2 节）：
1. (dm,opcode)=(3,0x66) 在 PROVEN 表中（TA opcode，DM3）
2. 本轮不创建 struct mt_gpu_vm（纯 harness ioctl）
3. 无卸载操作；safe_rmmod.sh 待命未需使用
4. Trial 前置：firmware 侧 pinned（dmesg connect=0 pinned=1 result=0），mt_guest_probe ref=13（r389 旧泄漏基线，r390 已证新路径无泄漏）
5. 一次一个 live 动作（单 harness 进程，串行两次 kick）；timeout 未使用

## 2. TA 回归验证（活体）

Harness（r377 既证格式，远端已清理）：
- /dev/dri/renderD128；INIT 0x40046445 + u32 module=2
- Bridge 0xc0206440，mt_pvr_cmd{0x82, 0xC, in_ptr, out_ptr, 268, 12}；IN 268B kick_ta=1@188；OUT 12B

第 1 次（ctx=0x0 → per-file 回退）：
- ioctl 返回 0；OUT.error=0；OUT.update_fence=4
- dmesg [ 8468.584083] musakickgfx2: submitted wire=4 → 精确匹配

第 2 次：
- ioctl 返回 0；OUT.error=0；OUT.update_fence=5
- dmesg [ 8481.107603] musakickgfx2: submitted wire=5 → 精确匹配

0x100 完成证据：dmesg 零 "TA wire=%u completion timeout"（grep -c=0）——pvr_ta_wait_complete() 在 2s 内收到了 firmware 的 0x100 完成事件（words[1]==0x100 且 wire_id 匹配），走 mt_marker_complete_ta() 正常退休，而非 pvr_ta_abandon() 超时路径。fence signal 隐含于成功回填（abandon 路径会 dma_fence_set_error(-ETIMEDOUT) 并打 WARN）。

健康：dmesg grep -cE 'WARN|BUG|Oops'=0；收尾 mt_pvr_bridge ref 0、mt_guest_probe ref 13（与轮前基线一致，无泄漏）。

## 3. 安全合规

- 本轮零模块操作：未重载 bridge（r391 构建在载）、未卸载任何模块、未重启。
- rmmod -f/--force：本轮零出现（harness 为纯 userspace ioctl）。
- timeout：未使用（TA 完成等待由内核侧 2s 超时 + abandon 兜底）。
- 一次一个 live 动作：两次 kick 串行执行，无并发。

## 4. 诚实边界

- 本轮验证的是 marker 级 TA 路径（零绘制），非真实 TA render payload；MT_TA_VM_READY 门保持关闭。
- 两次 kick 均走 per-file 回退（ctx=0x0）；per-context kick 路径已在 r391 V3 验证，本轮未重复。
- mt_guest_probe ref=13 基线含 r389 旧泄漏（12 refs），下次冷重启清零；本轮 delta 为 0，证无新泄漏。
- T1/T2 为静态扫描：in-tree 代码全覆盖；一次性探针模块（gitignored）仍靠清单人工执行——本轮无探针模块。

## 5. 门禁

- make -C mt-vgpu-guest check-offline：472 Python + 299 C 全绿
- make kernel W=1：未重跑（本轮无内核代码改动；r391 已验证零警告）
- 反向验证：不适用（本轮无代码改动；r394 的 RV1–RV3 覆盖安全测试本身）

## 6. 交付物

- 本报告：mt-vgpu-guest/reports/r395-safety-net-first-live-test.md
- 证据：mt-vgpu-guest/reports/r395-dmesg-ta-regression.txt（0600，8 行：4 dispatch + 4 submitted，r391 wire=2/3 + r395 wire=4/5）
- Harness 源码：~/workspace/ssh-remote/scratch/r395_ta_regression.py（本地保留，未入库；远端已清理）
