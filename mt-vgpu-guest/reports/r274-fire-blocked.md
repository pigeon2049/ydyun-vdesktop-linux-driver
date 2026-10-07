# r274：fire 活体失败（批准执行）——DM prepare 先倒，slices/fire 未达

- **结论**：三开（`=2` + tqx_ctx + fire）新构建 + 真实 blit，`prepare failed: -22` 重现（r268 只修了 space 上限一处，DM prepare 另有 -EINVAL 源）；`tqx slices:`/`fire`/`tqx-ctx: ready` 全无（DM bring-up 先倒，tqx 块未达）。blit 即时 SIGABRT（134，无 hanging/D 态）；refs 自归（probe 1/bridge 0）；拆桥干净，默认 + L3 全绿。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；盘内 `.ko` 含 fire 字符串（3 处）+ vermagic 对版；`mkdir build/traces/r274`（新约束流程）；dmesg 打 `[r274] fire-live-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_tqx_ctx=1 translate_tqx_fire=1`（probe 未碰，装盘前验 strings + vermagic）。
3. 真实 blit（`timeout -s KILL 90` 防护）：即时 134；trace 落硬盘暂存区（后清空）；dmesg 仅 `prepare failed: -22`（本轮），无 slices/fire/ready 行。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing；终态 1/0；dmesg 无新增 WARN。

## 边界

- 失败定位到 DM prepare 阶段（tqx 块之前）；候选：scratch/tqx Bo 绑定、space create、upload/seal、DM context——需分项打印（下轮）。
- 本轮未用裸 timeout 包裹 ioctl；无代码改动（r267/r268 代码已在盘）。

## 下一步（候选，需批准）

1. bring-up 分项打印 + 重验（离线小改 + 活体）：定位 -22 来源。
