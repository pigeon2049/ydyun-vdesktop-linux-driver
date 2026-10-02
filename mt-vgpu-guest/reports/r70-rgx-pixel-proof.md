# r70：RGX 像素级验证（render-target 读回）

r66 证明 fence 完成，本轮证明像素落地：纯用户态程序经标准 DRM ioctl
在自研 `mtvgpu` 节点上完成 3D 渲染执行 + 64 KiB VRAM 完整读回核验。

## 执行（全新会话，`live_3d_drm`，未改动在树代码）

- `insmod mt_live_3d_drm.ko`：2D VM（20 maps）+ 3D VM（23 maps，
  上限 15872 实测），注册 `mtvgpu 0.3.0`（`card2`/`renderD129`，
  bridge 的 `card1`/`renderD128` 不受影响）。
- 用户态用 `/tmp` 副本（仅把节点路径改成环境变量 `MT_NODE`，
  树内源码零改动）：`MT_NODE=/dev/dri/renderD128` 打不开——
  该节点是 bridge 的 PVR 节点，无 MT_* ioctl（`ENOTTY` 类失败），
  属预期；切到 `renderD129` 通过（root 执行，用户态 open 受限与
  AppArmor/ACL 相关，行为与 smoke 一致）。
- `mt-3d-check 1`：Frame 1（seq=1，120µs）+ Render Target 帧（seq=2，
  107µs）；目标先验 0x5a 填充，GPU 执行后 64 KiB 读回核验通过。

## 健康

- `Guest/FW 2/2`，`event_result=0`，事件数未增（两次提交零 fault）；
  `pending=0/completed=2`；34 对象/2 地址空间/2 上下文留存。
- 零 fault/HWR/Oops。`live_3d_drm` 不自 pin 但 sealed 空间不可卸载——
  保持加载，不触碰。本会话对象存储已满，后续需空存储的实验会被拒绝。
