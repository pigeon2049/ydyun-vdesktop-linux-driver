# r128：RT 绑定帧成功——非零 0x45a0 被接受，completed 再 +1，零 fault

STATUS 下一步第 1 项（真实绘制观察）方向的一小步：
r127 证无 RT 空包可执行，本轮证**带 RT 绑定的同一模板也可执行**
（`0x45a0` 非零路径活体通过）。仍是空 marker（无绘制内容），
像素零变化符合设计，不是失败。

## 执行（实测）

- `insmod mt_live_3d_drm.ko`（当时唯一 live 模块）→ `renderD129` 就绪。
- 提交器 `/tmp/opencode/r128-rt-frame.py`（一次脚本，不入库）：
  CREATE 64KiB GEM（handle=1）→ 16 页 `0x5a` 写入 → 读回全等 →
  `SUBMIT_3D{frame_tag=2, target=1, syncobj=0}` → QUERY 复核 →
  读回 → `GEM_CLOSE`。无 timeout 包裹。
- 结果：`ret=0 sequence=2 latency_us=128`；
  `submitted/completed 0→1`（本 load 计数），`faulted=0`。
  seq=2 接 r127 的 seq=1，跨 load 连续。
- 读回：65536 字节**全仍是 `0x5a`**（差异 0）。
  含义（推断，标注）：空模板本就不含 fill 内容（r70 的像素变化来自
  带绘制的参考包，不是本模板）；本轮断言的是**执行接受性**，
  不是像素变化——RT 绑定 Parsons（va/stride/extent 写入 + fence 挂接）
  全程无错即达目标。
- 附带修正：r127 脚本 QUERY 用 72 字节，正确 ABI 是 80
  （`mt-drm-check-common.h` 有 `_Static_assert(80)`佐证）；
  本轮脚本已用 80，r127 的读值因字段位置靠前不受影响（结论不变，
  特此订正方法瑕疵）。

## 卸载与遗留（实测）

- `rmmod`："unloaded cleanly"，2 条同签名 WARN（674/679，
  累计 4 条，无新模式）。
- probe 引用 35 → **61**（本周期 +26，与 r44 记录的 +26/周期吻合；
  r127 周期 +34，差异原因未解释——记为未知，不编造）。
- 桥探针复核 0 failing / 0 mismatch；bridge 引用 0；无 D/Z 任务。
- freeze 继续。

## 未做

- 真绘制内容（fill/三角）的像素变化仍无：要非零 CCB/绘制负载，
  走完整绘制路径（STATUS 第 1 项未竟部分）或 fill 帧——下一轮议。
- push：65 提交未 push（用户此前明确暂不 push）。
