# r136：驱动不写 /tmp；“几小时卡死”有三个已知候选，当前无泄漏在发生

- **结论**：内核驱动（`mt_guest_probe` / `mt_pvr_bridge` / `mt_live_3d_drm`）**没有任何写文件代码**，
  当前 `/tmp` 使用率 **1%（53M/7.9G）**，无持续写入。机器发生过**外部重启**（uptime 23 分钟，
  全部 `mt_*` 模块未加载，`/dev/dri` 仅 `card0`），r125–r134 的 retained 会话已不存在，
  所以**当前没有泄漏在发生**——“运行几小时卡死”只能是历史会话上的积累问题。
- 本轮零硬件触碰（只读：`df/du/ls/lsmod/dmesg/ps` + 代码 grep），未加载/卸载任何模块。

## 实测（执行过，有输出）

1. `/tmp` 现状：`df -h /tmp` → 1%；`du -sh /tmp/*` 最大是 Chromium 缓存（约 20M），
   `/tmp/opencode` 为空，`/tmp/mtt-linux-umd-*` 不存在（重启被清空，符合快照 §8.3 预期）。
2. 内核写文件搜查：`kernel/` 下 grep `filp_open|kernel_write|vfs_write` → **0 命中**
  （仅 `list_for_each_entry_safe(pmr, tmp, …)` 这类遍历变量名含 `tmp`，14 处全是迭代器，
   见 `mt_pvr_bridge.c:812-814`）。
3. `/tmp` 的真正写者（全是**用户态**，与内核泄漏无关）：
   - `probe/umd_bridge_shim.c:458`：`fopen($UMD_TRACE 或 /tmp/opencode/umda/trace.jsonl, "a")`，
     但有 **256MB 硬顶**（`UMD_TRACE_MAX_BYTES`，`:447`，bA13 两次灌满教训后加的，超限截断并 stderr 告警）。
     `read()` 默认不记（`:954-962`），只在 `UMD_TRACE_READ` 置位时记。
   - `Makefile:89-97`：L4 每级 `UMD_TRACE=/tmp/opencode/umda/l4-*.jsonl`。
   - UMD 解包目录 `/tmp/mtt-linux-umd-5.2.0/`（易失，树内留档可恢复，sha `b3058c02…`）。
4. 内存/模块现状：`lsmod` 无 `mt_*`；`MemAvailable 12.8G/16G`；`dmesg` 0 warn；
   无 D 态任务；`Slab 251M` 无异常。
5. 常驻占用（静态代码读数，非泄漏）：probe 常驻约 8M（firmware_backup `MT_FW_MAP_SIZE=0x800000`，
   `mt_mmu.h:28`）+ 1.1M（memory_snapshot，`mt_guest_probe.c:69`）+ trial 备份；
   bridge **每个 open fd** 固定 2M arena + 64K info 页
   （`mt_pvr_bridge.c:207-208,910,961`），release 路径成对释放（`:842-866`）。
   空载挂着不增长——增长只发生在反复 ioctl / 反复加卸载实验模块时。

## 推断（未实测，按证据强度排序；“几小时卡死”的候选）

1. **`live_3d_drm` 每周期 +26 probe 引用泄漏（最像，r44 起，已复现 4 次：r127 +34，r128–r130 各 +26，
   probe 引用 1→35→61→87→113）**。`retained` 在 `mt_live_3d_drm.c` 中**从未被置位**
   （只有 `:184` 读取和 `:939` `WARN_ON`，无赋值），属 dead flag；sealed VM 的 BO backing pin 未释放。
   反复跑 live 实验几小时 → 引用数持续爬升 + 对象存储占满（快照 §11：需空存储的实验会被 `-EBUSY` 拒绝）。
   表象就是“越跑越卡直至拒绝服务”。
2. **r67 device-mutex owner-death**：若卡死时表现为 MapPMR 永久挂起 + `rmmod` 解不掉，
   即此泄漏（`dev->mutex` 全局泄漏，唯一干净恢复是重启）。触发与 `timeout` 落在 bridge ioctl
   临界区内有关。当前无 D 态、无模块加载，可排除正在发生，但下次卡死时应首先查
   `ps` D 态 + `dmesg` MapPMR 挂起来确认/排除。
3. **Chrome 被动持有 renderD128**（r85 记）：bridge 每个 open fd 钉住 2M arena；
   若用户态进程反复 open 不 close，内存被 pin 住。属用户态行为，非内核泄漏，
   用 `lsof /dev/dri/renderD128` 可查（需 bridge 加载时）。

## 证伪记录

- “驱动往 /tmp 写东西导致卡死”：**证伪**。内核零文件写调用；用户态 trace 有 256MB 顶；
  且当前 `/tmp` 1%，卡死若发生也与 `/tmp` 满无关（§8.3 的 120→91 回归在本机当前不成立）。
- “空载放几小时也会漏”：**无证据**。常驻分配全是 probe/打开时一次性，后台 work
  （`mt_rpc_service.h:73` 250ms 轮询、`mt_live_service.c:87` 250ms、`mt_master_notify.c:113` 20s）
  只做应答查询，不分配常驻内存；`remove` 路径 `kvfree/vfree` 成对（`mt_guest_probe.c:1501-1549`）。

## 下一步

- 下次卡死**不要直接重启**：先留 `lsmod` 引用数、`ps` D 态、`dmesg`、`lsof /dev/dri/*`、
  `cat /proc/meminfo + /proc/slabinfo`，对号入座上面三条。
- 重建会话（r68/r69 流程）需用户明确批准；`+26/周期` 的根因定位（BO pin 归属）单独立项。
- 遗留：71+ 提交未 push（本轮前已存在）；`r135-major2-ccb-create.jsonl` 未入库文件在工作区，未动。
