# r332：冷启动后活会话重建（批准执行）——L3 全绿，freeze 生效

- **结论**：r330 冷启动后的新会话建成：trial `20261008T025100Z-c85ff8c5`（`result=0/connect=0/published=1/connected=1/pinned=1/registered=15`，`disconnect=-61` 系保留语义，固件 sha `35d40f75…` 与 r211/r264 一致）→ `mt_guest_probe` 绑定 `00:0e.0`（ref 1）→ `mt_pvr_bridge.ko` 默认参数在载（`card1`/`renderD128`，ref 0）→ L3 全绿（node 0 failing/0 mismatch，dma smoke PASS，refs 1→2→1，无 GPU 提交）。窗口零新增 WARN/BUG/Oops。**Freeze 即刻生效。**在载桥为 r331（含拼写收尾）新鲜构建。
- **与 r211/r264 的差异（实测）**：`mt_cold_disconnect finish=0` 被拒（`-EPROTO`）：`precheck failed: guest=0 fw=1 started=0`——冷启动把设备寄存器清零，无残留会话可拆。读源码确认 precheck 要求 `regs+0x890==2`（guest）；`guest=0/fw=1/started=0` 恰为 `finish=1` 完成态，直接跳过 cold 走 `fresh-trial`（其 `--help` 亦要求“未绑定 + Guest=0/FW=1”）。不是缺口，是干净结论。

## 实测（执行过）

1. 预检：无 `mt_*` 模块；`/dev/dri` 仅 `card0`；`00:0e.0` 无绑定；`/` 29%；kmsg 打 `[r332] rebuild-start`。
2. `fresh-trial.py --run --runtime-context` rc=0；trial 字段只读 `result.json`（r211 教训，不重跑）。
3. `insmod mt_pvr_bridge.ko`（默认参数）rc=0；L3 双绿如上；终态 probe ref 1 / bridge ref 0。
4. dmesg：仅未签名 taint（常规）+ cold 拒接行；零新增 WARN/BUG/Oops。

## 边界

- 本轮只做重建 + L3，未跑 L4、无 UMD 流量、无 GPU 工作；`translate_kick` 保持 off。
- r150 Oops 未复现但根因仍未命名（口径延续）。
- **Freeze**：不 rmmod、不 unbind、不提交额外工作；`make probe`/`make umd` 活会话上禁用。
- 下一刀（r328 已定义，需新一轮批准）：`[r8]` 出参槽读数——r317 core 离线读，或 GDB 断 abort 桩读调用者帧。
