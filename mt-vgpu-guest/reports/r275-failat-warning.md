# r275：fail_at 定位 + teardown WARNING 修复（批准执行）

- **结论**：fail_at 机制一次建功：`prepare failed at line 1856: -22` 直指定位 `bind_boot_shared` 调用（DM 阶段，tqx 块之前）。附带发现本轮首个内核 WARNING：teardown `cancel_work_sync` 未 INIT 的 fire_work（`__flush_work` WARNING，转储 10 行；触发条件：fire 从未提交即 rmmod）。修复：prepare 成功末尾 `INIT_WORK` 一次（fire 处不再重复 INIT）+ 前向声明。`make kernel` 零警告；`check-offline` 350 Python OK。拆桥干净，默认 + L3 全绿。**Freeze 已恢复。**

## 实测（执行过）

1. 离线：30 处 `goto out` 机械标记 fail_at（首版转义事故后手工修复打印行）；门禁 +1（行号上报断言）；反向（删 unlock 对即红——r265 门禁复用验证）。
2. 活体：三开 + blit（timeout 90 防护）→ `failed at line 1856` 首现即定位；blit 即时 134，无 D 态。
3. WARNING：`__flush_work+0x378`（`cancel_work_sync` 未初始化 work）； remnants：转储行留 dmesg（历史计数 +10，如实记录）；修复已编入盘内构建，**未活体复验**（下轮）。
4. 恢复：`rmmod` → `unloaded cleanly`（probe 25→1）；`insmod` 默认桥 → node/smoke 全绿；终态 1/0。

## 边界

- bind_boot_shared 内部 -22 来源仍未细分（mt_boot_bo_bind → pools → bind_many 链；下轮在 bind_many 加 fail 位或打 tables 地址）。
- tables 地址打印行缺失之谜未解（字符串在盘内 .ko，行未现；与 fail_at 无关，另记）。
- 未用裸 timeout 包裹 ioctl；本轮有代码改动（诊断打印 + INIT 移位）。

## 下一步（候选，需批准）

1. bind 内部细分定位（离线小改 + 活体）。
2. WARNING 修复复验（rmmod 无 WARNING）。
