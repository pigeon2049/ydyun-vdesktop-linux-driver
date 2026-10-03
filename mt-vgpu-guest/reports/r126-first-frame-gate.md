# r126：r113 首帧 fabricated 门禁落地——envelope 钉死，活体执行待批

本轮零硬件触碰（离线门禁 + 读码；会话保持 freeze，未加载/卸载任何模块）。
结论：r113 验收的 fabricated 一半已落成 8 项门禁并反向验证；
活体执行是真 GPU 提交，需明确批准，未动。

## 门禁（新增 `tests/test_r113_first_frame_envelope.py`，8 项，全绿）

- 模板解析自 `kernel/mt_gfx_packet_template.h`：`MT_GFX_LINUX_PACKET_BYTES`
  实测 `0x46f0`，数组实测 18160 字节，两者一致。
- RT 槽位：`0x45a0/45a8/45b0` 在模板中为零（空 marker 不绑 RT）；
  `0x4668` 在模板中为 `0xed00000000`（见下）。
- `+0x08` 在模板中为零（tag 逐 kick 动态写，不 baked）。
- 与 `kernel/recovery/mt_live_3d_drm.c` 交叉一致：tag 写 `0x08`、
  四个 RT 槽位写点都在 `if (target_lease)` 之后、
  `req.bytes = MT_GFX_LINUX_PACKET_BYTES` + `req.type = 3`（DM2）。
- `first_frame_envelope(tag)` 构造期望首帧字节：模板 + `+0x08=tag`，
  且除 `+0x08` 外与模板逐字节一致（tag=1/2 双例）。

## 新发现（实测 + 推断，已区分）

- **实测**：模板 `0x4668 = 0xed00000000`（非零），初版门禁"RT 全零"
  假设当场失败，已按实修正——门禁抓到了我自己的错误假设，
  方向正确。
- **推断**：该值是参考执行 baked 的 VA，无 RT 时 ioctl 不覆写、
  随包直通固件；DM2 空包接不接受（含此直通值）仍是唯一的活体未知项。
  以活体为准，门禁只钉"当前值"，不断言"合法"。
- **反向验证**：临时把 define 改为 `0x46F1` → 门禁 1 项失败；
  还原后全绿，`kernel/` 工作区干净。

## 门禁总数

L1：226 → **234** Python（+8），268 C 不变；STATUS 与快照 §6
计数已同步，快照 §12 未动（运行态无变化，会话仍 freeze）。

## 未做（等明确批准）

- 活体首帧：`mt_live_3d_drm` 当前**未加载**，执行 = 加载实验模块 +
  真 GPU 提交（`type=3`，fence 等待），超出 r125 重建批准范围。
  批准后步骤（下一轮）：insmod `_drm` → 无 RT + `frame_tag=1` 首帧 →
  断言 `completed+1`/fence 连续/零 fault → 报告；模块按红线做完即卸。
- push：62 提交未 push（用户此前明确暂不 push）。
