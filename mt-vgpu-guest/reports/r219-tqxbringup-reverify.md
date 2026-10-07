# r219：TQX bring-up 新会话复验通过（批准执行）——`=2` + `translate_tqx_ctx` 下 `tqx-ctx: ready`

- **结论**：TQX bring-up 在新会话+新构建上复验通过：桥以 `drm_major=2 translate_tqx_ctx=1` 重载（probe 未碰，`translate_transfer` 保持 off——纯 bring-up，最小动参），真实 `musa_blit_test -device 0 -f -o`（passthrough 记录）单次 `0x89:0xa` 后桥报 `submit3 tqx-ctx: ready`（与 r182 成功签名一致，无 `-22` 回归、无 `sealedmiss`）。UMD 随后用户态 SIGABRT（134，即时退出，无 hanging、无需外部杀）。bring-up 期间 probe ref 1→28；拆桥 `unloaded cleanly`，probe 28→1（-27 对称归零）。桥恢复默认 + L3 全绿，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：refs 1/0（r216 构建在载）；`/tmp` 2%；UMD/blit 路径就绪；dmesg 打 `[r219] tqx-bringup-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2 translate_tqx_ctx=1` rc=0，节点仍 `renderD128`。
3. 真实 blit：8201 行 trace（与 r174/r214 同行数，单 submit 落 seq 8201），observe 行 VA/尺寸/res/PMR/39B/首偏移全同，`+0x40` 取第四个相异值 `60 70`（37/39 一致；计数器解释再添一反例）。证据：`reports/r219-tqxbringup-reverify.jsonl`（已入库）。
4. ref 记账：bring-up 后 probe 28（translator 持有 +27；r182 成功轮为 +18，量级差异诚实记录——本轮 UMD 即时 SIGABRT，r182 为 hanging 后 timeout 终结，退出路径不同；对称性成立即无泄漏，归因不展开）。
5. 恢复：`rmmod` → `unloaded cleanly`，probe 28→1；`insmod` 默认桥 → node probe 0 failing/0 mismatch + dma smoke PASS；终态 ref 1/0；dmesg `WARNING|BUG|Oops` 计数 0。

## 边界

- bring-up 成功 ≠ 可发射：submit/fence/落位仍未验证（r182 口径延续）；`translate_kick` 全程 off；无 GPU 提交。
- 本轮一次只载一个 live 配置，做完即卸并恢复默认；未用 `timeout` 包裹 blit（进程即时退出，无需外部杀）。
- 无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射（submit+fence+落位+像素回读）：仍是未验证区分项，需专门立项。
- 真实 check/update 数组的 observer 流量；真实 3D producer recon（离线）；真实绘制执行（待 backend 接线）。
