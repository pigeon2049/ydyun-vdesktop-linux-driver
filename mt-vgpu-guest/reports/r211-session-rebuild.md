# r211：活体会話重建完成（批准执行）——新 trial 下 Guest/FW 2/2 pinned，桥默认加载，L3 全绿

- **结论**：本机已重启进 `6.12.111`（旧 r166 会话随重启消失，`00:0e.0` 无绑定、无模块在载，freeze 自然解除）；用户明确批准真机测试后重建：`cold_disconnect finish=0/1`（rings idle，`guest=0 firmware=1`，双 clean rmmod）→ `fresh-trial.py --run --runtime-context`（rc=0，新 trial `20261007T040408Z-f3fb55af`，firmware sha `35d40f75…` 与 r138/r166 一致，`guest=2 firmware=2`，pinned=1，ref=1）→ `mt_pvr_bridge.ko` 默认参数加载（`card1`/`renderD128`，ref 0）→ L3 全绿：`pvr_node_probe` 0 failing/0 mismatch，`pvr_dma_smoke` PASS（session refs 1→2→1，无 GPU 工作提交）。dmesg 无新增 WARN/BUG/Oops（r150 的 NULL deref 未复现）。**Freeze 即刻生效。**

## 执行序列（实测）

1. 预检：运行核 `6.12.111+deb13-amd64`，两 `.ko` vermagic 一致；`make kernel` + `kernel/recovery` W=1 零警告；`00:0e.0` 无 driver 绑定；`/tmp` 2%；dmesg 打 `[r211] rebuild-start` 标记；`sudo -n dmesg` 无历史 Oops/BUG（仅 fabricated harness 用户态 segfault 日志，非内核问题）。
2. `cold_disconnect finish=0`：`all rings idle, started=0, FW=1, result=0` → rmmod rc=0。
3. `cold_disconnect finish=1`：`state after write: guest=0 firmware=1 started=0, result=0` → rmmod rc=0。
4. `fresh-trial.py --run --runtime-context` rc=0：`result=0 connect_result=0 published=1 connected=1 pinned=1 registered=15 online=1`，`guest=2 firmware=2 started=1`；`disconnect_result=-61`（与 trial 保留语义一致，见 r166 同形）；dmesg `firmware trial: connect=0 disconnect=-61 restored=0 pinned=1 result=0` + `experimental Guest transport bound; trial_bus_master=1`。注：为取 trial 字段多跑了一次 `--run`（无新 trial 生成、无新增 dmesg 行，会话无损；下次只读 `result.json`，不再重跑）。
5. `insmod mt_pvr_bridge.ko`（默认参数）rc=0：`registered 'pvr' node`，`card1`/`renderD128`。
6. L3：
   - `pvr_node_probe /dev/dri/renderD128`：`OK: 0 failing step(s), 0 value mismatch(es)`（含未知 ioctl 拒绝项）。
   - `pvr_dma_smoke /dev/dri/renderD128`：`PASS`，`session refs 1→2→1`，无 GPU 提交。
7. 终态：probe ref 1，bridge ref 0，`/dev/dri` = `card0`（QXL）+ `card1`/`renderD128`（桥）；dmesg 仅 arena/DMA/VM plan 信息行，无模块相关 WARN。

## 边界

- 本轮只做会话重建 + L3（节点探针 + DMA smoke），未跑 L4、无 UMD 流量、无 GPU 工作提交；`translate_kick` 保持 off。
- r150 Oops 未复现，但根因仍未命名——后续加载 live 实验模块仍一次一个、做完即卸。
- **Freeze**：不 rmmod、不 unbind、不提交额外工作；`make probe`/`make umd`（会先 rmmod）继续禁用。
- r210 的 fabricated GFX CCB（`r210-gfx-ccb-capture.bin`）仍是离线字节，不因本轮重建获得任何执行语义。

## 下一步（候选，需批准）

- 真实绘制 CCB 活体验证（STATUS 下一步 #1）：DDK2 render backend 与 TA/3D 执行链仍缺真实接线（r207/r208 边界），不得用 accept-and-log 代替执行。
- 同步 update 语义活体验证（STATUS 下一步 #2）：r159 离线结论 + r203 fabricated `flag=2` update，待真实 `0x82:0x14` handler。
