# r125：新会话重建完成——Guest/FW 2/2，L3/L4 全绿（批准执行）

用户已批准重建（r124 问答），本轮按 r68/r69 流程一次走完。
结论：新 retained 会话已活并 freeze；在盘桥（111 构建，
build-id `2c6bede3…`）在载；L3 三探针 + L4 八级阶梯全部通过。

## 执行序列（实测）

0. 只读预检：无 `mt_*` 模块，`/dev/dri` 仅 `card0`，
   `00:0e.0` 无驱动绑定（本机此次启动连 `mtgpu` 驱动都没出现，
   无需 unbind），`/tmp` 2%，`mt-status --read-status`（sudo）
   回 `driver_state=2, firmware_state=1`——与 r44/r68/r69 的重启残留一致。
1. `insmod mt_cold_disconnect.ko`（finish=0 只读）：6 DM 全环 idle、
   `started=0 FW=1 result=0`，`fw_pa=0x77dfef000`（与 r59 同值）→ rmmod。
2. `insmod ... finish=1`：`guest=0 firmware=1 started=0`
   （模块回读 + mt-status 双重确认）→ rmmod helper。
3. `fresh-trial.py --run --runtime-context`：`load_rc=0`、
   `connected=1/published=1/pinned=1`，`disconnect_result=-61`
   （保留式预期值），`guest=2 firmware=2 started=1 retained=1`，
   `module_sha256=6425a32a…`（与 r124 canvas 一致，即 111 在盘构建）。
   证据：`build/fresh-trials/20261003T162138Z-604b34a6/`（gitignore，不入库）。
4. `insmod kernel/recovery/mt_pvr_bridge.ko`：`card1`/`renderD128` 出现，
   文件 build-id `2c6bede3a3cb3851f5eadef5bada150a5d83fb9a`
   （`.note.gnu.build-id` 实测；`/sys` notes 为空故以文件为准，
   加载的即此文件，无歧义）。
5. L3：`pvr_node_probe` 0 failing / 0 mismatch；
   `pvr_dma_smoke`（`timeout 120` 挂起探测）`PASS … no GPU work submitted`，
   session refs 1→2→1；`pvr_kick_probe` PASS（`ufo_known=2/3`，
   dmesg 有 `check=2 update=1` 两行）。
6. L4：UMD 由树内留档恢复到 `/tmp`（sha `b3058c02…` ✅），
   rung1–rung8 逐级 `exit=0`（含 rung4 render 与 rung8 kicksubmit），
   trace 落 `/tmp/opencode/umda/`（易失，不入库）。

## 终态（实测）

- `mt_guest_probe` 引用 1（ retained 会话 pin 住），
  `mt_pvr_bridge` 引用 0；`/dev/dri` 有 `card1`/`renderD128`。
- dmesg 零 WARNING/BUG/Oops；0 个 D 态任务。
- **Freeze 即刻生效**：不 rmmod、不 unbind、不提交额外工作。
  r113 首帧执行是下一轮（另行确认是否属于"提交 GPU 工作"范畴，
  见遗留）。

## 未做 / 遗留

- r113 首帧翻译执行待下一轮：check-only 设计虽只读观察，
  但要过 `0x88:0x4` 以外的提交形态，进新会话第一笔工作前需单独确认。
- push：连同本轮，61 提交未 push（用户 r124 明确暂不 push）。
