# r258：硬盘暂存区流程验证（批准执行）——AGENTS.md 约束落地 + `/tmp` 清零

- **结论**：用户纠正（tmpfs 仅 8G）后，AGENTS.md §5 已改判：大体积易失产物 MUST 写硬盘 `mt-vgpu-guest/build/traces/`（gitignore，按轮子目录，用完即清），NEVER 写 `/tmp`。本轮落实：`/tmp/opencode/umda/` 从 4.2M 清零（已入库的 16 个 trace 删 /tmp 副本；未入库的 soak/并发/GDB 中间产物确认无独特证据后删除；shim 默认 `trace.jsonl` append 缓冲删除）；新建硬盘暂存区并验证流程——默认桥下 ping 全 PASS + fabricated rung8 全过，`UMD_TRACE` 显式指向 `build/traces/r258/rung8.jsonl`（25KB 落盘）；验证后暂存区已清空，`df` 无压力（/ 27%，/tmp 2%）。refs 1/0 不变（默认桥，未重载），dmesg 干净。**Freeze 继续。**

## 实测（执行过）

1. `git check-ignore build/traces/` 确认 gitignore 覆盖；`mkdir -p` 建区。
2. dmesg 打 `[r258] disktrace-start` 标记；ping 全 PASS（raw ioctl，无 trace）；fabricated rung8 全 0、`rung8.jsonl` 25KB 落硬盘。
3. `rm -rf build/traces/r258` 清空；`df -h` 复核。
4. 事后 refs 不变；dmesg 零 WARNING/BUG/Oops。

## 边界

- 本轮是流程验证轮，无新语义证据；rung8.jsonl 未入库（回归副产品，已清）。
- 未用 `timeout` 包裹；无代码改动（AGENTS.md 约束变更除外）；未跑门禁（纯文档+约束变更）。

## 下一步（候选，需批准）

- GDB 确认 kick 字段偏移 + fabricated GFX 重建（r257 下一步，离线）；TQX 真发射立项；CCB 解读；backend 接线。
