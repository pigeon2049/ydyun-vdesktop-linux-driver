# r414：真实 TA 首次活体执行成功——固件 0x100 完成（219µs）

> 轮次：r414（2026-10-09）。**最高风险轮。完成 r413 未竟之事，真实 TA 首次活体成功。**
> 背景：r412 实现完成但被 trial 阻塞；用户冷重启后 r413 确认 trial 重建成功；本轮补加分发+活体单发。

## 结论

**真实 TA 包首次被固件接受并执行：`status=0`，`COMPLETED (0x100)`，提交到完成仅 219µs。**

- DM 包布局验证：**VA @+0x28 / size @+0x30 正确**（r411 [INFERRED] → 本轮 [MEASURED]）
- TA 条目语义验证：40B 简单条目（64×64 dummy）被固件接受
- 完整路径：`0x82:0xFE` → `pvr_cmd_ta_real_test` → 360B→BO[10]@4096 → `mt_bridge_submit_ta_work` 真实路径 → DM3/opcode 0x66 → 固件 0x100

## 分发行补加

r413 卡点：r404 重构后空白字符为 `\tcase …:\t\t\t/*…*/` + `\t\treturn …`（非脚本预期的 2-tab/1-tab），自动插入失败。

本轮手动补加（`build/traces/r414/apply.py`，exact-match 校验）：
1. `#include "../mt_ta_real.h"` + `pvr_cmd_ta_real_test`（169 行，r412 hook.c 原样）插入于 `pvr_dispatch_rgxta3d` 之前
2. `case 0xFE:`（3-tab 注释对齐，与周边一致）加于 `MT_PVR_FN_RGXKICKTA3D5` case 之后（~行 5360）
3. `make kernel` W=1：**零警告**（强制重编验证）

## 活体证据

dmesg（`build/traces/r414/dmesg-r414.txt`，0600）：
```
[  787.971539] mt_pvr_bridge: r412: TA real test, ctx=0x1000
[  787.971555] mt_pvr_bridge: r412: TA buf written to va=0x7a001000 size=360
[  787.971611] mt_pvr_bridge: r412: submitted wire=1, waiting
[  787.971830] mt_pvr_bridge: r412: COMPLETED (0x100) wire=1
[  787.971831] mt_pvr_bridge: r412: test done status=0 result=0
```

用户态：`INIT(2)→Connect→Create(0x12, handle=0x1000)→Test(0xFE)` → `status=0 result=0` → `FIRMWARE COMPLETED (0x100) - SUCCESS`

关键时间：提交（.971611）→ 完成（.971830）= **219µs** —— 真实执行，非超时。

## 验证结论

| r411 推断 | 本轮实测 | 状态 |
|---|---|---|
| VA @+0x28/+0x2c | 固件读取并完成 | [MEASURED] ✅ |
| size @+0x30 (=360) | 固件读取并完成 | [MEASURED] ✅ |
| 40B 条目 Q2 打包 | 固件接受 | [MEASURED] ✅ |
| Q0/Q1/Q3/Q4 最小构造 | 固件接受（dummy 值） | [MEASURED] ✅（语义仍待深挖） |

## 安全

- Pre-live T1/T2/T3：10 tests 全过（重建后重跑）
- `(3,0x66)` 白名单确认；一次桥加载→单发→`safe_rmmod.sh` 卸载（ref=0）
- `timeout` 未进临界区；无 oops/WARN/hang；未尝试重启
- `mt_guest_probe` 未动（ref=1，drm 持有，frozen）
- 测试钩子未提交（工作区 UNCOMMITTED，门控 `MT_TA_REAL_PACKET=1` 为测试构建）

## 门禁

- `make -C mt-vgpu-guest check-offline`：**480 Python + 299 C 全绿**
- `make kernel` W=1：**零警告**
- 反向验证：未做（活体成功即最强验证；门控反向已在 r411 验证）

## 交付物

- 本报告 `reports/r414-real-ta-first-live-success.md`
- 证据 `build/traces/r414/dmesg-r414.txt`（0600）、`build/traces/r414/apply.py`
- `reports/README.md` +1 行；`MEMORY.md` 顶部插入 r414；`PROGRESS-SNAPSHOT.md` §12 追加 r414
- 本地提交（不 push）

## 诚实边界

- 360B 内容为最小 dummy 构造（64×64，Q0/Q1/Q3/Q4 语义未深挖）；固件接受≠语义正确
- 未验证像素输出（T2 回读仍缺）；未验证多条目/复杂条目
- 生产路径仍为 marker（门控默认 0）；本轮测试构建未提交
- 单发成功，稳定性/重复性未测

## 下一步

1. **P0**：将真实 TA 路径产品化（门控开启流程、DM 布局固化、生产代码提交）
2. **P1**：T2 回读验证（像素级确认 TA 真实执行了绘制）
3. **P2**：3D 真实包（`MT_3D_SUBMIT_GATE`，r406 已证基础设施）
