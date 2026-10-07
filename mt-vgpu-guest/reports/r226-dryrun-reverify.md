# r226：transfer dry-run 新会话复验通过（批准执行）——digest 与离线预言逐位一致

- **结论**：`translate_transfer` dry-run 在新会话+新构建上复验通过：桥以 `drm_major=2 translate_transfer=1` 重载（probe 未碰，`translate_tqx_ctx` 保持 off——纯 dry-run，最小动参），真实 `musa_blit_test -device 0 -f -o` 后桥报 `submit3 dry-run: pool=0x1032 pixels=1310720 color=0xff0000ff va=0x8000a43000 1280x1024 fnv=0xd893618ca42d3711`——与 r181 离线预言逐位一致（输入→程序映射在新会话成立）。UMD 即时 SIGABRT（134，无 hanging）。拆桥 `unloaded cleanly`（probe ref 自归 1，全程无持有残留）；桥恢复默认 + L3 全绿，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r226] dryrun-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_transfer=1` rc=0，节点仍 `renderD128`。
3. 真实 blit：8201 行 trace（同构）；observe 行 VA/尺寸/res/PMR/39B 全同，`+0x40` 取第五个相异值 `2a 9a`（37/39 一致；计数器解释继续出局）。证据：`reports/r226-dryrun-reverify.jsonl`（已入库）。
4. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node probe 0 failing/0 mismatch + dma smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- dry-run 不提交：GPU 执行、fence、像素回读一概未发生；digest 一致只证明“程序字节正确”，不证明“打得动”（r181 口径延续）。
- 本轮一次只载一个 live 配置，做完即卸并恢复默认；未用 `timeout` 包裹 blit（进程即时退出）。
- 无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射（submit+fence+落位+像素回读）：仍是最大未验证区分项，需离线实现先行。
- CCB 内容解读（GFX 系包头/payload，离线）；真实绘制执行（backend 接线，离线大工程）。
