# r276：WARNING 修复验证（批准执行）——INIT 前移后失败路径 teardown 干净

- **结论**：r275 WARNING 的真条件定位并修复验证通过：WARNING 来自 prepare **失败路径**的 teardown（非 exit），因 fire_work 只在成功末尾 INIT。修复：INIT 移到 prepare 入口（幂等，失败路径亦覆盖）。三开 + blit（prepare 必失败）重验：`failed at line 1862`（行号+6，机制自证）之后**零 WARNING**（标记后计数 0；旧转储行留历史缓冲）。拆桥 `unloaded cleanly`（probe ref 自归 1）；桥恢复默认 + L3 全绿（node + smoke）。**Freeze 已恢复。**

## 实测（执行过）

1. 开工：直接 rmmod 在载默认桥（ref 0）——零新增 WARNING（预期内：translator 从未 prepare，exit 跳过 cancel；r275 真条件反证）。
2. 离线：INIT 前移 + 前向声明已在 + 门禁新增（INIT 先于 acquire 断言）；`make kernel` 零警告；`check-offline` 351 Python OK（350+1 新）。
3. `rmmod` → `insmod drm_major=2 translate_tqx_ctx=1 translate_tqx_fire=1`（probe 未碰；dmesg 打 `[r276] warnfix-verify` 标记）。
4. 真实 blit（timeout 60 防护）：即时 134；`failed at line 1862: -22`（bind_boot_shared 未解，另案）；标记后 WARNING 计数 0。
5. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node/smoke 全绿；终态 1/0。

## 边界

- bind_boot_shared 内部分项仍未解（-22 来源另案，不在本轮）。
- 未用裸 timeout 包裹 ioctl；本轮有代码改动（INIT 移位 + 门禁）。

## 下一步（候选，需批准）

1. bind 内部细分定位（离线小改 + 活体）。
