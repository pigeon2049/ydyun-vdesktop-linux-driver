# r121：r113 修正案——DM2 固定 0x46f0 包，"空"即模板本体

r113 设计的执行可行性离线验证一则，结论利好：
DM2 提交不是变长包，而是**固定 `0x46f0` 字节信封**
（`MT_GFX_LINUX_PACKET_BYTES`，`live_3d_drm` 写死），
而 tree 内模板头自述为 "Generated minimal legal Linux 3D packet"、
源自已验证的 libsrv 参考执行——即模板本体（+frame_tag、
不绑 RT）**已是最小合法形状**，"空 marker"不需要发明新编码，
就是模板减去 RT 绑定。r113 的"DM 空包接受性"未知项收窄为
"无 RT 模板固件是否接受"（单点，活体检）。
离线读码，零硬件触碰。

## 细节

- 信封结构（`mt_gfx_packet.h`）：record 起 `0x90`（`0x3580`B）、
  CSW `0x3620`；RT 绑定在 `0x45a0/45a8/45b0/4668`
  （`live_3d_drm.c:559-567`，`target_lease` 为空即跳过——
  代码天然支持"不绑 RT"路径，无需改代码）。
- frame_tag `+0x08`（`r->frame_tag` 非零即写）是追踪位，
  首帧 tag = kick 序号的设计（r113）可直接用。
- 故首帧翻译的 DM2 侧**零新增代码假设成立**：
  现有 `_drm` ioctl + 空 RT 参数即达"空 marker"；
  唯一活体问题是固件接不接受（r64 DM1 空 marker 有先例，
  DM2 无）。
