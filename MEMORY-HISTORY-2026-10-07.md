# MEMORY HISTORY — 2026-10-07

> 由 `MEMORY.md` 清理周期移入（只保留最新两节），原样保留。

## 本次会话进展（r207：KickTA3D5 wire 固定与执行链盘点）

- 零硬件触碰。加入 hash 对版 5.2 `MUSAKICKGFX5` 的 108/4 wire 结构与偏移静态断言；gate 映射到 `0x82:0x14` UMD size row，未接 dispatch。
- 执行链盘点：render2 context 只是 handle token；sync handle 可到 PMR；现有 kick translator 只发固定 marker，TDM Submit3 只做观察/dry-run。没有能执行 UMD TA/3D CCB 的完整 backend。
- 验证：`make check-offline` 全绿（295 Python、1 skip、292 C）；`make kernel` W=1 零警告。
- 证据：`reports/r207-kickta3d5-wire-foundation.md`。后续需接真实 context、nested sync/PMR arrays 和 TA/3D completion；live 仍待明确批准。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r206：KickGFX5 Host schema 映射）

- 零硬件触碰。对版 5.2 DKMS 包 SHA=`e3f684b1…` 的生成头将 `0x82:0x14` 映射到 `MUSAKICKGFX5 +20`；schema 的 flags/VA/size/submissionID/count 偏移和值逐项匹配 r203 fabricated trace。
- 12B 偏移差现已由额外 flags 与 submissionID 字段解释；同包 `MTGPUMUSAGFX5KM` 声明给出 sync/PMR 数组与提交参数接口顺序，但没有 handler 实现体；下一步核对 Guest handle 台账和实际 CCB 执行路径。
- 证据：`reports/r206-kickgfx5-schema.md`、r203 trace、`downloads/mthreads-dkms_5.2.0_amd64.deb`。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r201：fabricated GFX update producer 动态复核）

- 零硬件触碰。GDB 对上 kick `+0x28` 目标 `+0x200` 与 render-context allocator；修正 `CreateSyncPrim` 输出槽后，CheckSync/UpdateSync 各一项，update `flag=2`、handle 非空，trace 发出 fabricated `0x82:0x14`（IN108/OUT4）。shim 返回后在 `local_e70` update-list 清理处 heap abort；r203 证明根因为 harness 输出复制越界，扩大缓冲后 UMD 可干净返回。
- 证据：`reports/r201-gfx-update-producer.md` + `r201-gfx-update-producer.jsonl`。后续追踪见 r202/r203。

---

## 本次会话进展（r200：fabricated render context allocator 实测）

- 零硬件触碰。fabricated 默认 shim 下 connect/device/devmemctx/render 全返 0；返回 context `+0x200` allocator 和 `+0x318` SubmissionHead 均非空，trace 117 行。r201 已将其 allocator 与 GFX kick `+0x28` 目标动态对上。
- 证据：`reports/r200-renderctx-allocator.md` + `reports/r200-renderctx-allocator.jsonl`。后续查清 r201 清理 abort。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r199：追踪 GFX submission allocator 首参来源）

- 零硬件触碰；同 SHA UMD 二进制指令核对：RGXKickGfx 从 kick `+0x28` 所指对象的 `+0x200` 取 SubmissionCmdGenerate 首参。render-context 构造器在 context `+0x200` 建 SubmissionBufAlloctor；SubmissionHead 是另一个对象、作为第二参。r198 的空终值原因未动态区分。
- 证据：`reports/r199-gfx-submission-allocator-origin.md`。下一步 GDB 逐级读取 kick `+0x28`、目标 `+0x200`、kick `+0x2d8` 与真实 render-context `+0x200`，再按验证后的字段复放。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r193：psKickTA 构造）

- 零硬件触碰。psKickTA 无铸造函数；锚点是真实 render 上下文
 （`+0xc`）；`+0x1c8` 由 PrepareTA 回填；features `+0x54` 门内
 第二次出现。下步手塑回合可在 fabricated shim 下离线做，
 无需硬件批准。
- 证据：`reports/r193-taskickta-shape.md`。候选下一步：手塑回合（离线）/ 活体 ladder（待批）。

---

## 本次会话进展（r194：psKickTA 手塑首轮）

- 零硬件触碰（fabricated）。harness 手塑 psKickTA 调通
 `RGXKickTA → 3` 干净退出：4 崩溃逐一定位（`+0x2d8/+0xb8/+0x28`）；
 关键纠偏——PrepareTA 偏移是元素制（`+0xb6` 实为字节 `0x2d8`）。
 下步造 `flag&2` 条目看 3 是否翻提交。GDB 翻车 3 则已记。
- 证据：`reports/r194-takick-shaping.md` + `r194-takick-return3.jsonl`。
 候选下一步：手塑回合 2（离线）/ 活体 observer（待批）。

---

## 本次会话进展（r195：flag&2 手塑改走 producer 层）

- 零硬件触碰（fabricated）。连接对象写入 flag=2 sync 条目后 `RGXKickTA → 3`，trace 无 kick ioctl。SHA 对版调用图表明 `RGXKickTA` 不调用 `SubmissionSetUpdateSyncPrim`；RGXKickGfx 等才走该 helper。r194 选错测试入口，下一步转 producer 层。
- 证据：`reports/r195-takick-flag2.md` + `r195-takick-flag2.jsonl`。会话仍 freeze；未跑 live observer。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184 排序，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---

## 本次会话进展（r196：RGXKickGfx update producer）

- 零硬件触碰（fabricated）。`RGXKickGfx` 到达 `SubmissionSetUpdateSyncPrim`，实测 count=1、首项 flag=2；trace 发出 `0x82:0x14`（IN 108/OUT 4），fake shim 返回 0，函数返回 0。r197 更正：之前手动清零的 render-context `+0x20/+0x24` 是 perf callback AppHint 字段；真实 update-list 初始化在 `RGXPrepareTA`，动态验证待做。
- 证据：`reports/r196-gfx-update-producer.md` + trace；r197 已更正 poke 字段解释。下一步保留 perf defaults，按 psKickTA 输入条目重放；会话保持 freeze。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---

## 本次会话进展（r197：纠正 RGXKickGfx update 初始化解释）

- 零硬件触碰；离线 UMD SHA 与语料一致。r196 写零的 render-context `+0x20/+0x24` 是 PerfCountStart/EndCbID；r198 另证 `+0x24` 同时作为 PrepareTA 状态表索引。update list 是 RGXPrepareTA 单独分配并将新对象 `+0x24` 计数置零，再从调用者 psKickTA 复制条目。临时 musa.ini 已无 poke 越过 PrepareTA，但卡在空 submission context；动态 update helper 验证待做。
- 证据：`reports/r197-correct-gfx-update-init.md` + `reports/r198-gfx-apphint-replay.md`。下一步追 RGXKickGfx 的 SubmissionCmdGenerate 首参来源。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---

---

## 本次会话进展（r198：无 poke AppHint 初始化并追到 SubmissionCmdGenerate）

- 零硬件触碰（fabricated）。同版 UMD 通过临时 `musa.ini` 将 `PerfCountEndCbID=0` 初始化到 render context；无对象内存 poke。GFX 越过 `RGXPrepareTA`，但在 `SubmissionCmdGenerate` 因首参为空 SIGSEGV；109 trace 行无 `0x82:0x14`。不宣称 update helper 已动态复验。
- 证据：`reports/r198-gfx-apphint-replay.md` + trace。新发现 `+0x24` AppHint 字段同时参与 PrepareTA context 状态表索引；update-list count 的 `+0x24` 属于另一个新分配对象。
- 下一步按 r199 指令核对结果，GDB 逐级确认 kick 输入和 allocator 对象是否对应；会话保持 freeze。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，和 STATUS/§12 活页有差异；留待快照刷新 pass 一并校正。

---

## 本次会话进展（r202：update-list 生命周期核对）

- 零硬件触碰。`FUN_00178800` 按 count 分配 update-list 并复制条目；r203 动态定位覆盖 update-list chunk header 的输出越界并在扩大 harness 缓冲后消除 abort。
- 证据：`reports/r202-update-list-lifetime.md`。后续动态复核见 r203。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r203：GFX update fabricated 干净返回）

- 零硬件触碰。GDB watchpoint 证明 `RGXKickGfx` 在 RVA `0x7ee1a` 将 0x408 字节复制到仅 0x80 bytes 的 b24，覆盖 update-list chunk size（`0x91→0x1151`），造成 r201 free abort。b24/b25 扩为 0x410 后 header 完整，`0x82:0x14` 发出且 RGXKickGfx 返回 0、进程正常退出。
- 证据：`reports/r203-gfx-update-clean.md` + trace。下一步离线核对并补齐 bridge `0x82:0x14` handler ABI；真实 CCB 仍冻结待批准。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r204：KickTA3D5 ABI 边界）

- 零硬件触碰。r203 fabricated trace 确认 `0x82:0x14` 为 108/4；SHA 对版 UMD wrapper 传入长度 108。2.7.1 生成结构编译为 96/4，2.3 Guest 无此结构，当前 dispatcher 缺 handler。5.2 Host schema 审计大小匹配但不能证明 Guest 支持。
- 结论：暂不把 2.7.1 结构直接用于该 UMD 请求，先恢复 108 字节逐字段契约并确认目标 Guest handler 语义。
- 证据：`reports/r204-kickta3d5-abi-boundary.md`，r203 trace，`reports/legacy-umd-pvr-bridge-abi.json`。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---

## 本次会话进展（r205：KickTA3D5 字段偏移）

- 零硬件触碰。SHA 匹配 UMD wrapper 栈布局与 r203 seq 123 fabricated 请求互证数组指针及三个 count 的偏移和值：check=1、update=1、sync PMR=0。
- 尾部 `0x48`–`0x5f` 有未定语义参数槽；与 2.7.1 `0x48` 起 VA/size/count 的布局不同，12 字节差异属于结构偏移问题，handler 仍不可安全补齐。
- 证据：`reports/r205-kickta3d5-field-offsets.md` + r203 trace。下一步查找 5.2 生成头或目标 Guest handler 对尾部字段的定义/读取路径。
- 遗留：`PROGRESS-SNAPSHOT.md` §5 仍停在 r184，留待快照刷新 pass。

---


## 本次会话进展（r208：PMR 到真实 GPU VM 的后端边界）

- 零硬件触碰。只读确认 `mt_bo_system_borrow()` 能将稳定 `mt_system_memory` 的逐页 GPA 包成设备 session BO；VM 绑定要求 BO 与页表 BO 的 store/ops 一致。现有 PVR PMR `gpu_bo` 是 CPU-only planning facade，不能进入真实 VM。
- 真实接线仍需 per-file 上传 VM、process/render context、PMR borrowed BO 生命周期、nested sync/PMR 解引用、CCB 资源闭包和 fence 完成。当前 translator 全局共享、context 只是 token；marker/TDM observer 不执行真实 CCB。
- 未改代码、未跑门禁、未动硬件。设计路线和证据边界见 `reports/r208-ddk2-render-backend-boundary.md`。STATUS 下一步不变。
- 遗留：真实 CCB 活体验证仍须用户明确批准；执行包格式/资源闭包尚未证实。

---
## 本次会话进展（r209：fabricated GFX CCB 捕获能力）

- 零硬件触碰。扩展 `umd_bridge_shim`：`0x82:0x14` 按 5.2 schema 的 submission VA/size 找 shared PMR backing；显式设置 `UMD_CCB_DUMP_DIR` 后才将原始 CCB 落盘，16 MiB 上限、0600、独占创建。`0x89:0xa` 复用同一逻辑。
- 新增合成门禁覆盖偏移、PMR 解析、raw bytes、不可解析 VA、关闭 shared backing 与关闭 dump 目录。故意把 size offset 84 改成 80 时测试失败（128 vs 256），复原后通过。
- `make check-offline`：295 Python（1 skip）+292 C 全绿；`git diff --check` 通过。未改内核、未运行 `make kernel`、未操作硬件。
- r203 现存请求可解出 VA=`0x8000023000`、size=`0x4700`、flags=0、submissionID=1；本轮没有重放 GFX producer，因此真实 UMD CCB backing bytes 尚未捕获。见 `reports/r209-kickgfx-ccb-capture.md`。
- 遗留：使用可复现的 fabricated GFX producer 配方捕获真实 UMD 生成 CCB，并据其字节结构推进 TA/3D 包映射；捕获/解码仍不等于执行。

---
## 本次会话进展（r210：fabricated GFX 原始 CCB 捕获）

- 零硬件触碰。恢复重放工作目录与 sync tuple 后，GDB 实测 check/update helper 均返回 0，`RGXKickGfx` 返回 0；trace seq 125 发出 `0x82:0x14`，VA=`0x8000023000`、size=`0x4700`、check/update counts=1。
- r209 shim 落盘 18,176 字节原始 UMD CCB，107 字节非零，SHA-256=`faa93985aa6b3f65df66641ac73f25020a66fca7af6cdece3b9e88c3a315dae7`。保存于 `reports/r210-gfx-ccb-capture.bin`，桥 trace 在同名 `.jsonl`。
- 不证明 Guest handler 或 GPU 执行；STATUS 下一步仍是补齐真实 DDK2 render backend 和 TA/3D CCB 执行链。详见 `reports/r210-gfx-ccb-capture.md`。
- 遗留：USB/画面线短页 `docs/PROGRESS.md` 标题日期仍为 2026-10-03；本轮是 vGPU 任务，留待对应短页刷新轮处理。

---
## 本次会话进展（r211：活体会話重建，批准执行）

- 本机重启进 `6.12.111`，旧 r166 会话消失（`00:0e.0` 无绑定、无模块在载）。用户批准真机测试后重建：`cold_disconnect finish=0/1` 均 rings idle、`guest=0 firmware=1`、双 clean rmmod；`fresh-trial.py --run --runtime-context` rc=0，新 trial `20261007T040408Z-f3fb55af`（firmware sha `35d40f75…` 与 r138/r166 一致，Guest/FW `2/2` pinned，ref 1）。
- `mt_pvr_bridge.ko` 默认参数加载（`card1`/`renderD128`，ref 0）；L3 全绿（node 0 failing/0 mismatch，dma smoke PASS，refs 1→2→1，无 GPU 提交）；dmesg 无新增 WARN/BUG/Oops。`make kernel` + `kernel/recovery` W=1 零警告。
- **Freeze 即刻生效**：不 rmmod、不 unbind、不提交额外工作。详见 `reports/r211-session-rebuild.md`。
- 遗留：真实绘制 CCB 活体验证（STATUS 下一步 #1）与 update 语义活体验证（#2）仍待真实 DDK2 render backend 接线；r210 fabricated CCB 仍是离线字节。USB/画面线短页标题日期仍为 2026-10-03，留待对应轮处理。

---
## 本次会话进展（r212：check-only 新会话复验，批准执行）

- r211 新会话上 legacy check-only 复验通过：桥以 `translate_kick=1` 重载（probe 未碰），r73 配方首跑全绿（check 值直接用 r148 实测值 0），六符号全 0、`0x88:0x4 ret=0`，dmesg `translated kick: check=1 update=0 tag=1 fence=1`（与 r148 同形）。
- 拆桥 `unloaded cleanly`，probe ref 25→1；桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。trace 130 行已入库 `reports/r212-checkonly-reverify.jsonl`（不再放易失 `/tmp`）。详见 `reports/r212-checkonly-reverify.md`。
- **Freeze 已恢复**：不 rmmod、不 unbind、不提交额外工作。
- 遗留：DDK2 check-only（r149 对应项）仍待复验；真实绘制 CCB 仍待 backend 接线。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r213：DDK2 check-only 新会话复验，批准执行）

- r211 新会话上 DDK2 check-only 复验通过：桥以 `drm_major=2 translate_kick=1` 重载（probe 未碰），`0x82:0x12`/`0x88:0x5`/`0x88:0x4` 全 `ret=0`，dmesg `translated kick: check=1 update=0 tag=1 fence=2`（与 r149 逐字同形）。
- 首跑沿用 legacy 链形（CCB 传 `b5`）在 CCB create 后用户态 SIGSEGV——正是 r144 定论的 `b5*` 间接缺失；仅改该传参后即绿。桥侧干净回收，无内核异常。trace 249 行已入库 `reports/r213-ddk2checkonly-reverify.jsonl`。详见 `reports/r213-ddk2checkonly-reverify.md`。
- 拆桥 `unloaded cleanly`，probe ref 25→1；桥恢复默认 + node probe 0 failing，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**
- 遗留：真实绘制 CCB 仍待 DDK2 render backend 接线；update 语义活体待 `0x82:0x14` handler。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r214：真实绘制第二样本，批准执行）

- r211 新会话上真实 `musa_blit_test -device 0 -f -o`（`=2` 桥，shim 仅 passthrough 记录）发出单次 `0x89:0xa`（8201 行 trace，与 r174 次轮同行数）；observe 行 VA/尺寸/res/PMR/39B/首偏移与 r174 全同；39 非零字节 37 跨会话一致，仅 `+0x40` 取第三值 `33 57`。执行级比对（非推断）+ 源码核对 observe 回 0 无执行。
- UMD 随后用户态 SIGABRT（r172 同例），内核侧干净；拆桥干净，桥恢复默认 + L3 全绿（node + smoke），dmesg 零 WARNING/BUG/Oops。trace 已入库 `reports/r214-realblit-sample2.jsonl`。详见 `reports/r214-realblit-sample2.md`。
- **Freeze 已恢复。**
- 遗留：真实绘制执行仍待 backend 接线；update 语义活体待 `0x82:0x14` handler。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r215：`0x82:0x14` observer 离线实现，零硬件触碰）

- 开工声明零硬件触碰。STATUS #2 的 concrete 缺口（r190）闭合一半：新增 `pvr_cmd_kickta3d5_observe`（r174 模式：108B 定界 + render 上下文鉴权 + VA→reservation→PMR 三重定界 + 非零/FNV/head 统计 + 标量上报回 0，不读嵌套指针、不执行、无 fence）；分发接 `case MT_PVR_FN_RGXKICKTA3D5`（wire.h 新宏，r188 惯例）。
- 门禁 7 项新 + `fn_ids` 55→56，反向掐断验证通过；`check-offline` 302 Python + 292 C 全绿；`make kernel` W=1 零警告。未加载模块（在载桥仍旧构建，observer 待批准窗口重载验证）。详见 `reports/r215-kickta3d5-observer.md`。
- 活体 GFX kick 方向止损：无现成生产者脚本，翻炒 GDB 手塑链风险收益不成正比；真实 3D producer 仍 open（r190）。
- 遗留：真实执行仍待 backend 接线；update 活体待重载窗口。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r216：新构建上机 + L3，批准执行）

- 积压的 6 个本地提交已 push（`104fdeb..fff3769`）。r215 新构建（含 `0x82:0x14` observer）上机：装盘前验 strings + vermagic；单桥重载（probe 未碰），节点仍 `renderD128`；L3 全绿（node + smoke，refs 平衡），终态 ref 1/0，dmesg 零 WARNING/BUG/Oops。详见 `reports/r216-newbuild-reload.md`。
- observer 已在载但尚无真实流量（parked，不是 proven）；路由活体 ping 需新工具代码，留待下轮。**Freeze 已恢复。**
- 遗留：真实 GFX producer 仍 open；真实执行仍待 backend 接线。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r217：observer 分发活体验证，批准执行）

- r215 observer 分发在活体证明到达：新工具 `pvr_observe_ping` 发零填充 108B `0x82:0x14`（bogus context）回 `-ENOENT` 而非 `-ENOTTY`；control `0x82:0x1f` 仍 `-ENOTTY`。fresh file 即开即关，refs 1/0 不变，dmesg 零新增。工具零警告构建 + 5 项门禁（含反向）。`check-offline` 307 Python OK。详见 `reports/r217-observe-ping-live.md`。
- **会话未动，无需重载，freeze 继续。**
- 遗留（r218 已推进：合法 envelope 全路径走通，阵列仍 NULL）：真实数组流量仍待 producer。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r218：observer 全路径活体验证，批准执行）

- observer 全路径首次在活体走通：`pvr_observe_ping` 扩展为 11 步（ping/control/envelope 四建/fire/teardown 五项），11/11 ok exit 0；桥 dmesg 行标量全上报（check=1 update=1，零窗口零统计）。阵列全 NULL（门禁级不解引用），只证明全链不证明数组语义。详见 `reports/r218-observe-fullpath-live.md`。
- 门禁 5→8 项（含一次无效反向后的精确反向验证）；`check-offline` 310 Python OK。refs 1/0 不变，dmesg 零新增。**无重载，freeze 继续。**
- 遗留：真实数组流量仍待 producer；真实执行仍待 backend 接线。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r219：TQX bring-up 新会话复验，批准执行）

- TQX bring-up 在新会话+新构建上复验通过：`=2` + `translate_tqx_ctx=1`（`translate_transfer` 保持 off）重载，真实 blit 后 `submit3 tqx-ctx: ready`（无 `-22` 回归）；UMD 即时 SIGABRT，无 hanging。probe 1→28→1 对称归零；桥恢复默认 + L3 全绿，dmesg 干净。附带第四个 `+0x40` 轮变值（`60 70`）。trace 已入库。详见 `reports/r219-tqxbringup-reverify.md`。
- **Freeze 已恢复。**
- 遗留：TQX 真发射仍未验证；真实数组流量仍待 producer；真实执行仍待 backend 接线。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r219：TQX bring-up 新会话复验，批准执行）

- TQX bring-up 在新会话+新构建上复验通过：`=2` + `translate_tqx_ctx=1`（`translate_transfer` 保持 off）重载，真实 blit 后 `submit3 tqx-ctx: ready`（无 `-22` 回归）；UMD 即时 SIGABRT，无 hanging。probe 1→28→1 对称归零；桥恢复默认 + L3 全绿，dmesg 干净。附带第四个 `+0x40` 轮变值（`60 70`）。trace 已入库。详见 `reports/r219-tqxbringup-reverify.md`。
- **Freeze 已恢复。**
- 遗留：TQX 真发射仍未验证；真实数组流量仍待 producer；真实执行仍待 backend 接线。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r220：SyncPrimSet 真写离线实现，零硬件触碰）

- 开工声明零硬件触碰。“打通卡点”落到值语义链真卡点：`0x2:0x2` 从 stub 改真写（wrapper/生成头/活体三重互证 IN16；复用 translator 解析 + `index*4` 定界 + host 写；无 fence/提交/wakeup）。`0x2:0xd` 仍越界。**注意（r221 修正）：真 setter 实为 `0x2:0xa`，handler 挂错位置待搬移。**
- 门禁 7 项新 + `ddk2_render2` 改判 + wire MAPPING 补两行（生成表 16/4 diff 通过）；双重反向验证；`check-offline` 317 Python + 292 C 全绿；`make kernel` W=1 零警告。未加载，会话未碰。详见 `reports/r220-syncprimset-write.md`。
- 遗留：非零值 kick 活体待下轮批准窗口（重载 + raw set + kick 链）。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r221：非零 kick 活体发现，批准执行）

- 活体推翻两个离线假设：① UMD `SetSyncPrim` 实际发 `0x2:0xa`（objdump 实锤 `mov $0xa,%edx`；Ghidra 伪 C 写错 fn id；真身是跳板）——r220 handler 挂错位置，下轮搬到 `0x2:0xa`；② check-only 翻译不等 UFO 值（value=1 vs PMR=0 一次通过，fence=3），源码系 `if (nupdate)` 门控（r174 引入，疑笔误），r212/r213 只证明机械不证明值匹配。详见 `reports/r221-nonzerokick-findings.md` + trace。
- 拆桥干净（probe 25→1），默认恢复 + L3 全绿，dmesg 干净。**Freeze 已恢复。**
- 遗留：SyncPrimSet 搬移 + `if (nupdate)` 修复各独立成轮；非零 kick 双腿复验待搬移后。USB 短页标题日期问题留待对应轮。

---
## 本次会话进展（r222：非零 kick 双腿闭环，批准执行）

- r221 两处证伪本轮闭环：handler 搬到 `0x2:0xa`（新宏，生成头同名）+ `if (nupdate)`→`if (ncheck)`；门禁改判（syncprimset/render2/fn57/MAPPING）+ translator 新增 wait 门控断言；双重复位验证；`check-offline` 318+292 全绿；`make kernel` 零警告。
- 活体（`translate_kick=1`）：Leg1 预置+匹配 0.045s 即过（`SetSyncPrim→0`，fence=4）；Leg2 失配 5.007s 后 UMD 37（等待真实，无 marker）。probe 25→1 对称，默认恢复 + L3 全绿，dmesg 干净。双 trace 已入库。详见 `reports/r222-nonzerokick-closed.md`。
- **Freeze 已恢复。**值语义至此真闭环（r212 机械 → r221 证伪 → r222 双腿）。
- 遗留：update 非零腿；TQX 真发射；真实执行 backend。USB 短页标题日期问题留待对应轮。

---
