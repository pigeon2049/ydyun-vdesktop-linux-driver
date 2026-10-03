# r127：活体首帧执行成功——DM2 空包 seq=1，completed 0→1，零 fault

用户以"继续"批准 r113 活体执行（r126 问卷未弹回，以本轮确认为准；
如越权请指出，本报告如实记录决策链）。
结论：r113 首帧闭环走通——无 RT + `frame_tag=1` 的 DM2 空包真实执行，
fence 连续，零 fault；`0x4668` 直通值固件接受（r126 的未知项消除）。

## 执行（实测）

- 预检（只读）：probe 引用 1 / bridge 引用 0，`renderD128` 在位，
  dmesg 零 WARN，无 D/Z 任务。
- `insmod mt_live_3d_drm.ko`：`card2`/`renderD129` 出现，
  `2D pages=18 maps=20` + `3D pages=21 maps=23`（与 r44 一致），
  probe 引用 1→36（实验模块 pin，预期内）。
- 提交器 `/tmp/opencode/r127-first-frame.py`（一次脚本，不入库）：
  QUERY 基线 → `SUBMIT_3D{flags=0, frame_tag=1, target=0, syncobj=0}` →
  QUERY 复核，无 timeout 包裹（内核 fence 等待上限 3s，有界）。
- 结果：`ret=0 sequence=1 latency_us=117`；
  `submitted/completed 0→1`，`faulted=0`，`last_sequence=1`。
  验收三项（r113 §验收）全过：completed+1、fence 序号连续、零 fault；
  像素面无断言（无 RT，符合设计）。
- `rmmod mt_live_3d_drm`（做完即卸）："unloaded cleanly"，但留两条
  WARNING（`release_unpublished+0xe8/+0x12d`，即 674/679 行 sealed VM
  `-EBUSY`）——与 r44 同一已知现象，非新 bug，不静默。
  内核 taint 现为 `12800`（OE + 新增 W 位，与 r44 一致）。

## 遗留影响（实测）

- probe 引用 stays **35**（1→36→35）：sealed-VM 卸载泄漏类（r44 记
  +26/周期，本构建 +34），BO backing pin 未释放。会话健康无碍
  （见下），但**对象/引用记账已非初始态**——后续需空存储或精确计数的
  实验必须先读记账，不能假设干净。
- 会话健康复核：`pvr_node_probe` 0 failing / 0 mismatch（只做此一项，
  不补跑 ladder/DMA，避免额外工作）；bridge 引用 0；无 D/Z 任务。
- freeze 继续：桥与主模块保持加载，不再提交工作。

## 未做

- `0x4668=0xed00000000` 直通被固件接受——r126  fabricated 门禁的
  "只钉值、不断言合法"策略正确，门禁无需修改。
- push：63 提交未 push（用户此前明确暂不 push）。
