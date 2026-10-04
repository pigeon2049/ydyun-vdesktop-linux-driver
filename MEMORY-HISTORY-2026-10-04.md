# MEMORY-HISTORY-2026-10-04（只读归档）

> 本文件是 `MEMORY.md` 的溢出归档，只读。状态冲突时以
> `STATUS.md` → `PROGRESS-SNAPSHOT.md` → `MEMORY.md` 为准。

---

## 本次会话进展（r123：意外重启后复核；只读）

- 外部重启：树完好（b4e0b5a，干净）、L1 全绿（+1 诚实 skip）、
  UMD 留档可用；会话已失（无模块、设备解绑、对象清零）。
- §12 活页已更新 + STATUS 现状加 stale 标注；重建待明确批准。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r123-post-reboot.md`。
- 遗留：重建批准；加载窗口；push；T3 首帧执行。

---

## 本次会话进展（r124：内核 107→111 漂移评估；只读+离线）

- 运行内核已是 6.12.111（107 headers 并存）；在盘桥 vermagic 实测即
  111，可直接加载，重建不需重编；L1 全绿（226+1 skip，268 C）。
- 会话仍失（无模块、设备解绑、仅 card0）；canvas 脏文件随 test 提交刷新。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r124-kernel-drift.md`。
- 遗留：重建批准（含 r113 首帧执行）；59 提交 push 待明确指令。

---

## 本次会话进展（r125：新会话重建完成；批准执行）

- cold-disconnect（finish=0/1）→ fresh-trial（2/2 retained）
  → 桥加载（build-id `2c6bede3…`）→ L3 全绿 → L4 八级全 0。
- 终态：probe 引用 1 / bridge 引用 0，dmesg 干净，无 D 任务；
  会话即刻起 freeze。证据：`mt-vgpu-guest/reports/r125-session-rebuild.md`。
- 遗留：r113 首帧执行下一轮单独确认；61 提交未 push（用户明确暂不 push）。

---

## 本次会话进展（r127：活体首帧执行成功；批准执行）

- 单帧 DM2 空包（frame_tag=1，无 RT）：seq=1，completed 0→1，
  faulted=0；`0x4668` 直通值被接受；模块已卸（留 2 条 r44 同类 WARN）。
- probe 引用 stays 35（已知泄漏类）；桥探针复核全绿，会话健康，继续 freeze。
- 证据：`mt-vgpu-guest/reports/r127-first-frame-live.md`。
- 遗留：T3 下一步（非零 CCB 仍缺，STATUS 第 1 项）；63 提交未 push。

---

## 本次会话进展（r126：r113 首帧 fabricated 门禁；离线）

- 新增 `tests/test_r113_first_frame_envelope.py` 8 项全绿并反向验证；
  L1 226→234，STATUS/快照 §6 计数已同步；会话仍 freeze。
- 新发现：模板 `0x4668=0xed00000000` 直通（活体风险，已标注）；
  STATUS 桥 build-id 旧值攒入刷新 pass。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r126-first-frame-gate.md`。
- 遗留：活体首帧执行待明确批准；62 提交未 push；STATUS 桥 id 刷新。
