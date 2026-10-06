# r138：会话重建 + Δ1 二分：空载 +25，带帧 +26（批准执行 ①）

- **结论**：r137 的静态模型在活体上**精确复现**。新会话基线 probe ref=1；
  首个 live 周期 **+34**（= 25 + 一次性 boot borrow 9，`maps=20/23` 与静态计数一致）；
  第二个空载周期 **+25**；第三个带帧周期（3/3 completed，零 fault）**+26**。
  故 Δ1 = 提交路径残留（恰 +1，与提交数无关），其余 25/周期 = sealed fini 泄漏。
- 每次卸载固定 2 条 destroy WARN（`release_unpublished+0xe8/+0x12d`，
  累计 6 条），与模型一致；`dmesg` 无其他 WARN/BUG/Oops。

## 执行序列（实测，按 r125 流程）

0. 预检：`00:0e.0` = `1ed5:0222` 无驱动绑定；probe vermagic `6.12.111` 与运行内核一致；
   固件 sha `35d40f7509…` ✓；`/tmp` 1%；dmesg 打 `[r138] rebuild-start` 标记。
1. `cold_disconnect finish=0`：rings idle、`FW=1`、`result=0` → rmmod。
2. `cold_disconnect finish=1`：`guest=0 firmware=1`（mt-status 双重确认）→ rmmod。
3. `fresh-trial.py --run --runtime-context`：insmod rc=0，`connected=1/published=1/pinned=1`，
   runtime published，errors 空。终态 `Guest/FW 2/2`，ref=1。
   证据：`build/fresh-trials/20261004T065633Z-546d5bc5/`（gitignore，不入库）。
4. 二分（ref 经 `/sys/module/mt_guest_probe/refcnt` 实测）：
   - A 空载：insmod → 即卸：1 → **35**（+34；maps 20/23 实测）。
   - B 空载：insmod → 即卸：35 → **60**（+25）。
     注：首试 `mt-fill-check` 未加 sudo，`open` EPERM，无帧提交——反成干净的空载对照。
   - C 带帧：insmod → `sudo mt-fill-check renderD128 smoke`（2 fills + copy，
     `submitted=3/completed=3`）→ 卸：60 → **86**（+26）。
5. 桥 + L3（会话配平）：`insmod mt_pvr_bridge.ko`（在盘新鲜构建，含 `drm_major` 参数，
   `modinfo -p` 确认）→ `card2`/`renderD129`（minor 顺延，非 card1）。
   `pvr_node_probe renderD129`：**0 failing / 0 mismatch**（默认节点 renderD130 不存在，
   须显式传参）。`timeout 120 pvr_dma_smoke renderD129`：**PASS**，
   session refs 86→87→86（bridge 路径引用平衡，无泄漏）。L4 未跑（桥上无 UMD 流量需求，
   风险最小化； bridge 健康由 L3 覆盖）。

## 终态（freeze 即刻生效）

- `mt_guest_probe` ref **86**（构成：1 会话 pin + 34 + 25 + 26，二分实测链完整可复算），
  `mt_pvr_bridge` ref 0；`/dev/dri` 有 `card2`/`renderD129`。
- dmesg 6 条 WARNING（全是 destroy -EBUSY，无新模式）；无 D 态（jbd2 属常态）。
- **Freeze**：不 rmmod、不 unbind；② 的修复验证（重载 live 模块）属已批准动作，
  到时按轮次执行。

## 遗留

- Δ1（提交路径 +1/周期）精确对象未定位：候选为首提产物（timeline/syncobj/GEM lease 之一），
  待 ② 修完 fini 后重跑二分（空载应 +0，带帧若仍 +1 则单独猎杀）。
- 73+ 提交未 push；`r135-major2-ccb-create.jsonl` 未入库文件仍在，未动。
