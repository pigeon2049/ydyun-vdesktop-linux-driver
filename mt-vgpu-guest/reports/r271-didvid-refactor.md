# r271：DID/VID 收敛（离线实现，零硬件触碰）——17 文件 guard 合一

- **结论**：r270 横向排查的第二项落地：S3000 四字段 guard（`0x1ed5/0x0222/0x1ed5/0x1101`）从 17 个 recovery 文件收敛到 `mt_guest_match_s3000()` helper（`mt_guest_device.h`，与 BAR2/SEG5 宏同处）。宽松语义保留 5 处（drm_snapshot/irq_recover/live_service 无子系统、bridge device 查找、device_profile），如实声明未碰。门禁更新（helper 钉死 + 17 文件调用/无裸 quad 断言；注释 11→17 修正）；`check-offline` 347 Python + 292 C 全绿；`make kernel` 零警告。**未加载，会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明；`lsmod` 开工收工一致。
2. 代码：helper + 17 文件替换（含 4 文件补 include；irq_recover 初次误判宽松为四字段，已 revert；device_profile/profile 语义不同未碰）。
3. 反向验证：改 helper 子设备值 → FAIL；还原 → OK；重编零警告。
4. 全门禁 + 内核构建全绿。

## 边界

- 纯机械收敛，语义零变更（宽松处未收紧）；requirements.json 未动。

## 下一步（候选，需批准）

1. fire 函数（离线实现+门禁）。
