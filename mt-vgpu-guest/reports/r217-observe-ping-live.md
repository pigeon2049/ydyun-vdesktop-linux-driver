# r217：`0x82:0x14` observer 分发活体验证（批准执行）——ping 证明路由到达，control 证明无 blanket 放行

- **结论**：r215 observer 的分发已在活体上证明到达：新工具 `pvr_observe_ping`（raw sender，`pvr_kick_probe` 同款 preamble）向在载新构建发零填充 108B `0x82:0x14`（render_context=`0xdead`），桥回 `-ENOENT`（observer 鉴权拒绝）而非 `-ENOTTY`——分发 case 活着；control `0x82:0x1f` 仍 `-ENOTTY`，无 blanket 放行。fresh file 即开即关，无对象残留（refs 1/0 不变），dmesg 无 observe 行（鉴权先失败，符合设计）、零 WARNING/BUG/Oops。**会话未动，无需重载，freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0（在载即 r216 新构建）；`/tmp` 2%；dmesg 打 `[r217] observe-ping-start` 标记。
2. 工具（离线部分）：`probe/pvr_observe_ping.c`（~130 行，复用 `bridge_call`/`mt_pvr_cmd`/`0xc0206440` 字面量与 version/INIT/Connect 序列；尺寸取 wire struct `sizeof`，句柄取非零 `0xdead`）+ `probe/Makefile`（all/clean 各一行）+ 门禁 `test_pvr_observe_ping.py` 5 项（ping/ENOENT 期望/control/ENOTTY/结构体/非零句柄）；`-Wall -Wextra -Werror` 零警告构建。
3. 反向验证：工具内 `ENOENT` 改 `ENOTTY` → 门禁 FAIL；还原后 OK（中途一次 sed 误伤 control 行已被精确修复并复验，见门禁 OK）。
4. 活体：`pvr_observe_ping /dev/dri/renderD128` → 两项 ok，`PASS`，exit 0。事后 refs 1/0，dmesg 无新增（`kickta3d5|WARNING|BUG|Oops` 零命中）。
5. `check-offline`：307 Python（302+5 新）OK；C 门禁与内核构建沿用 r215（本轮 C/内核零改动，未重跑）。

## 边界

- 只证明分发到达 + 鉴权拒绝；observer 的窗口统计/上报路径（需合法 render context + reservation + PMR）仍无真实流量；执行语义更未碰。
- 本轮新增探针工具是只读 ping（两发 raw ioctl 即关文件），风险类同 L3；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- observer 全路径活体：合法 render context + 真实 CCB 窗口的 `0x82:0x14` 观察（需 GFX producer，仍 open；或 harness 构造最小合法输入，离线先行）。
- 真实 3D producer recon（离线）；真实绘制执行（待 backend 接线）。
