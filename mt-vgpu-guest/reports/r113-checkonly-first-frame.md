# r113：check-only 首帧翻译设计（空 marker + 真 fence，全离线设计）

r79 承诺的首胜路径收敛为可评审设计：取一个真实 UMD check-kick
（`0x88:0x4 check=N update=0`，r73 已证活体可达），经 T1/T2
（桥内已只读验证）解析同步，等待 UFO 值满足后提交一个
**空 DM2 marker**（r64 的 DM1 空 marker 有先例），fence 与 DM
执行挂钩而非即时返回。像素零风险（无绘制内容），但走完全链路
（UMD→桥→DM→fence→用户态），是翻译器从 0 到 1 的最小闭环。
**设计文档，不写代码**（STATUS：输入规约先行）。

## 变换（输入 → 输出）

- IN（已齐）：`check_devvar_offset/value/ufo_block + count`
  （T1 拷贝，`copy_from_user` 可达性已证）→ `ufo_known` 解析
  （T2，r73 活体 `1/1`）→ `GPU PA + offset + 期望值`。
- 等待：轮询 UFO 页值（bridge 侧 CPU 读 PMR host，无需 GPU；
  EventObjectWait 恒成功不可用——r81 红线，等待语义自己实现）。
- OUT（DM2 空包）：复用 `live_3d_drm` 信封——`frame_tag+0x08` 递增
  （tag 即 kick 序号，可追踪）、**不绑 RT**（无绘制）、
  `submit_context(type=3)` 原样、`bytes = 空 marker 长度`。
- fence 语义变更（仅翻译 kick）：桥不再即时 signal，
  而把 DM fence 挂到 `update_fence_fd`（`drm_syncobj_replace_fence`
  现成，live_3d 同款）；`check_fence_fd/timeline` 按原样透传。

## 不在本设计内（诚实边界）

- update 侧（需 DDK2/特性开关，r78）：首帧要求 `update=0`，
  非零 update 的 kick 拒绝翻译（`-EOPNOTSUPP` 诚实失败，
  不静默降级）。
- CCB 内容（r56）：空 marker 不需要 CCB；真绘制仍缺。
- DM 空包接受性：r64 证 DM1 空 marker 可执行；DM2 空包未验证——
  首帧执行的第一个断言就是它（失败则退回"带最小 fill 的 marker"，
  有预案）。
- 执行地点：需新会话（对象满），属加载/会话窗口，不在本轮。

## 验收（届时）

fabricated：合成 check-kick → 期望 DM2 字节（ envelope + tag，
逐字节断言门禁）；活体（新会话）：提交 → `completed+1`、
fence 序号连续、零 fault；像素面不做断言（无 RT）。
