# r272：槽位号与驱动名收敛（离线实现，零硬件触碰）——20+22 处合一

- **结论**：r270 横向第三项落地：`pci_get_domain_bus_and_slot(0,0,PCI_DEVFN(14,0))`（20 处）收敛到 `mt_guest_find_s3000()`，`"mt_guest_probe"`（22 处）收敛到 `MT_GUEST_DRIVER_NAME`（均与 BAR2/quad 宏同处 `mt_guest_device.h`）。单次使用不碰（QXL slot/named、probe 自身定义点、`pci_request_*` 的 tag 字符串）；宽松语义 5 处延续（r271 口径）。旧门禁 `pmr_lifetime` 硬编码字面量按 r174 先例改判用宏。门禁更新（含反向：改宏值即红）；`check-offline` 347 Python + 292 C 全绿；`make kernel` 零警告。**未加载，会话未碰。**

## 实测（执行过，零硬件触碰声明）

1. 开工即声明；`lsmod` 开工收工一致。
2. 代码：helper + 宏 + 23 文件替换（含 7 文件补 include；irq 初判后确认其 slot 已换、仅 QXL 保留，正确）。
3. 中途漏 3 文件（live_service/package_probe/master_notify 不在首批 slot 列表），残留 grep 抓获后补齐。
4. 反向验证：改宏值 → FAIL；还原 → OK；重编零警告。
5. 全门禁 + 内核构建全绿。

## 边界

- 纯机械收敛，语义零变更；requirements.json 未动。

## 下一步（候选，需批准）

1. fire 函数（离线实现+门禁）。
