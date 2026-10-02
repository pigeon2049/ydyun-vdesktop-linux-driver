# r66：首次 RGX 真实执行（单帧 DM2 Universal）

经用户授权（r65 设计 + 重启）。复用树内已验证的 `mt_live_3d` 路径，
全新会话上单帧提交，不做 render-target 落盘。

## 执行

- `insmod mt_live_3d.ko enable=1 count=1`：`exit=0`，
  `completed_frames=1 last_result=0 last_sequence=1`，延迟 93µs。
- 会话：`Guest/FW 2/2 started=1`，`event_result=0`，事件数未增；
  DM0 保留 trial 痕迹，DM1/DM2 环全部 `head==tail`（已消费）；
  `pending=0/completed=1`。
- 资源：22 对象、13.9MB、1 地址空间、1 进程/上下文——sealed 3D VM
  按设计保留。`mt_live_3d` 不自 pin（引用 0），但 sealed 空间不可卸载，
  保持加载、不触碰。
- 无 fault/`0x101`/HWR、无 WARN/BUG/Oops。

## 含义与边界

- 这是本机经自研栈的第一次 RGX 执行（此前真实执行只有 TQX copy +
  marker）。证据是 fence 完成 + 固件无 fault，不是像素——像素读回是
  `live_3d_drm` + `mt-3d-check` 的事，不在本轮。
- translator T3 的提交原语（`submit_context` type=3 + sealed VM）至此在
  本会话上实证可用；缺的仍是“提什么”（CCB/命令流内容，r56）。
- 本会话已有 retained sealed 3D VM：后续任何需空对象存储的实验
 （含再次的 `live_3d`）会被 `-EBUSY` 拒绝；bridge 的 PVR PMR 路径不受
  影响（独立对象域）。
