# r230：双开组合验证（批准执行）——transfer + tqx_ctx 同轮三行同现

- **结论**：`=2` + `translate_transfer=1` + `translate_tqx_ctx=1` 双开组合工作：真实 blit 后 dmesg 三行同现——observe（VA/39B）→ `dry-run: pool=0x1032/color=0xff0000ff/1280x1024/fnv=0xd893618ca42d3711`（与 r181/r226 预言一致）→ `tqx-ctx: ready`。两开关正交无干扰。UMD 即时 SIGABRT（134，无 hanging）；拆桥 `unloaded cleanly`（probe 自归 1）；桥恢复默认 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r230] dual-param-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_transfer=1 translate_tqx_ctx=1`（probe 未碰），节点仍 `renderD128`。
3. 真实 blit：8395 行 trace（dry-run 下 UMD 多走 ~194 行 PMR/pool 查询）；observe 行 `+0x40` 取第六个相异值 `03 a6`（37/39 一致）。证据：`reports/r230-dual-param.jsonl`（已入库）。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node/smoke 全绿；终态 1/0；dmesg 计数 0。

## 边界

- dry-run/digest 仍不证明可发射（r181 口径）；bring-up 成功 ≠ 可发射（r182 口径）。
- 一次只载一个 live 配置；未用 `timeout` 包裹。
- 无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）。
