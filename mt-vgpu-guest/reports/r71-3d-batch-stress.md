# r71：3D 批量压测（20 帧 + render-target，像素闭环）

r70 单帧像素验证之后，在同一会话上按 r44 前例跑批量，确认环队列、
fence 计数与固件事件在连续提交下依然干净。

## 执行（`mt-3d-check 20`，经 `MT_NODE` 副本走 renderD129）

- 20/20 帧全部 `[OK]`，seq=3..22 连续（接 r70 的 seq=2，无断号）；
  首帧 3498µs（冷），其余稳定 ~60µs（最低 58µs）。
- 随后 render-target 帧 seq=23（79µs）+ 64 KiB 读回核验通过；
  用户态与内核两边计数一致：`submitted=22 completed=22 last_sequence=22`
 （本轮）对内核 `completed=23`（累计，含 r70 的 2 次）。
- DM2 环游标从 head=2 推进到 head=23，`head==tail`（固件已消费，
  非仅 fence 就绪）；`event_result=0`，事件数仍为 1——21 次真实
  GPU 执行零 fault。

## 健康与边界

- 会话 `Guest/FW 2/2`，`pending=0`；34 对象/2 地址空间留存；
  零 WARN/Oops；模块全部保持加载不碰。
- 本次未越过 64-slot 环回绕边界（游标 23），回绕证据沿用 r39；
  未测多进程并发提交；像素内容断言仍是 0x5a 预填 + 读回一致，
  非图像语义校验——这些是后续项，本轮到此为止。
