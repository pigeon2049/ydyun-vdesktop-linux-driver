# r277：bind 黑盒未打开（批准执行）——新打印行灵异缺失，-22 仍在其中

- **结论**：bind_boot_shared 内部 -22 来源细分**未果**：bind_many 4 处 EINVAL（args/valid/overlap/varange）+ bind_pools 前置 + build_pages overlap + plan 返回 + boot borrow，逐处加打印后活体**全无新行**，fail_at 仍报 1862（bind_boot_shared 调用）。bind 内部全链静默成功但返回 -22；且新加打印行（tables/bind ret/slices 系）系统性缺失（字符串在盘内 .ko，build-id 一致，旧打印行齐全）——打印缺失本身是未解现象（如实记录，不归因）。拆桥干净，默认 + L3 全绿。**Freeze 已恢复。**

## 实测（执行过）

1. 离线：bind_many 4 打印 + bind_pools 前置打印 + build_pages overlap 打印 + plan 返回打印 + boot borrow 打印 + bring-up bind 返回打印；`make kernel` 零警告；门禁未增（诊断打印，行为无变更；`check-offline` 351 OK）。
2. 活体（三开 + blit，timeout 60 防护）：blit 即时 134，无 D 态；`failed at line 1862: -22`；新打印行零行；refs 自归（probe 1/bridge 0）。
3. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing；终态 1/0；暂存区 `r277` 未建（命令笔误，trace 未落盘；下轮注意先 `mkdir -p`）。

## 边界

- 本轮是诊断轮，无行为变更；打印代码是否保留待定（若保留需门禁，另议）。
- 未用裸 timeout 包裹 ioctl；blit 行为（134）与 r274 同形。

## 下一步（候选，需批准）

1. 换手段定位（ftrace/kprobe 无符号位点困难；或 bind 返回值转储到 debugfs，只读）。
2. 接受 bind 黑盒，回退到 scratch 分块方案（绕过 8MB 大 VM，另立项）。
