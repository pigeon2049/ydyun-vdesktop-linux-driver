# MEMORY HISTORY — 2026-10-07

> 由 `MEMORY.md` 清理周期移入（只保留最新两节），原样保留。

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
