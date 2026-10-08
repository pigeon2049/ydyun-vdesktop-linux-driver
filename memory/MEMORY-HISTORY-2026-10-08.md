# MEMORY-HISTORY-2026-10-08（只读归档；原样移入，不回改）

## 本次会话进展（r279：64 页绕行验证，批准执行）

- Chrome 关闭后重载验证：`=2` + tqx_ctx 下 slices ready + tqx-ctx ready 全现（bind 在 64 页下通过，2112 黑盒绕过）；blit hanging 60s 被杀系 UMD 行为（无 D 态，可 rmmod，对称归零）。拆桥干净，默认 + L3 全绿；本轮窗口零 WARN。trace 已入库。详见 `reports/r279-space64-live.md` + `.jsonl`。
- **Freeze 已恢复。**无代码改动（r278 离线部分已在盘）。
- 遗留：fire 分块循环（离线）；分块 fired/verified 活体。USB 短页标题日期问题留待对应轮。
---

## 本轮进展（r280：fire 分块循环离线实现，零硬件触碰）

- 接盘盘内半成品（struct 数组化、submit/teardown 仍单 fence，构建已破坏）并收尾：全帧按 51 行/块拆 21 块（上限 64），复用 scratch 基址，21 fence 逐序等、验尾块内容；teardown 全放；中途失败全 put。
- 门禁：`test_pvr_tqx_fire.py` +3（切条/全等/全放），反向验证通过（首轮改宏名后缀因被子串包含未触发，作废记教训；删 `chunks=%u` 即 2 FAIL，还原即绿）。`check-offline` 354+292 全绿；`make kernel` W=1 零警告。
- **Freeze 继续（未加载，会话未碰）。**
- 遗留：分块 fired/verified 活体（需批准：`=2` + tqx_ctx + fire 三开 + 真实 blit，看 `chunks=21` + `verified=1`）。USB 短页标题日期问题留待对应轮。
---

## 本轮进展（r282：fire 独立模块离线实现，零硬件触碰）

- r281 绕行：新模块 `mt_live_tqx_fire` 自有 render 节点（`mtlivefire`，零 ioctl）+ 自有 64 页 space/TQX ctx/slices/scratch，直连 probe 会话，不碰 bridge/renderD128。分块 fire（21 块/21 fence/验尾块），root-only 单发，失败行号上报，锁序沿 r265。
- 门禁 9 新项（含反向；修门禁自身 `cls.run` 覆盖 bug）。`check-offline` 363+292 全绿；`make kernel` W=1 零警告。
- **Freeze 继续（未加载，会话未碰）。**
- 遗留：独立模块活体（需批准：insmod 不碰 bridge → 新节点 + fuser 空 → run → dmesg → rmmod）。USB 短页标题日期问题留待对应轮。
---

## 本轮进展（r281：分块 fire 活体被持有挡回，批准执行，未触硬件）

- 三开重载第一步即被拒：`renderD128` 被 PID 60651（会话桌面自身）持有，bridge ref 1，`rmmod` 报 `in use` 即停手（未 `-f`、未 insmod、无提交）。refs 仍 1/1，本轮窗口零新增 WARN。空暂存区 `build/traces/r281/`。
- 遗留：holder 释放（需用户侧，agent 关不掉自家桌面）后重跑三开验证（`chunks=21` + `verified=1`）；r280 代码在盘未上机。2 提交仍在本地未 push（用户选择暂不推送）。
---


## 本轮进展（r283：独立模块首次真发射全绿，批准执行）

- 自有节点 bring-up 一次过（`prepared=1` + `slices: ready` + `card2`/`renderD129`）；活体三处反馈即修：completed 门删除（终身计数≠就绪）/串行流（slices 外借语义）/verify 补 `upload_dev`。终轮 `fired=1 chunks=21 verified=1 bad=0/1310720 result=0`。
- 拆模块干净（节点消失，refs 回 1/1）；窗口零新增 WARN。门禁 366+292 全绿；`make kernel` 零警告；反向验证通过。
- **Freeze 已恢复（bridge/probe/renderD128 全程未碰）。**
- 遗留：合并策略 recon（fire 合进 bridge vs 保持独立）或更大矩形/soak。本地提交仍未 push。
---


## 本轮进展（r284：大矩形分块通用性活体，批准执行）

- 换参（1920×1080/`0xff00ff00`）重载 fire 模块：`fired=1 chunks=32 verified=1 bad=0/2073600`，与离线预言（34 行/块、32 块）逐项一致；颜色参数真实落地。拆模块干净，refs 回 1/1，窗口零新增 WARN。无代码改动。
- **bridge 未碰，freeze 继续。**
- 遗留：soak 重复性或合并策略 recon。本地提交仍未 push。
---


## 本轮进展（r285：fire 模块 soak 全绿，批准执行）

- 5 轮装/打/卸：轮轮 `fired=Y verified=Y chunks=21 result=0`，probe/bridge ref 轮轮 1→1，节点现即消；窗口零新增 WARN。无代码改动。
- **bridge 未碰，freeze 继续。**
- 遗留：合并策略 recon。本地提交仍未 push。
---


## 本轮进展（r286：legacy 基线复核，无重载）

- 当前构建+会话真实 blit：101 调用仅 `0x89:0x0` → -25（seq 8180），有序拆除，UMD 134；与 r244 同形，拒绝点复核成立。refs 全程 1/1，窗口零新增 WARN。无代码改动。
- **freeze 继续。**
- 遗留：合并策略 recon。本地提交仍未 push。
---


## 本轮进展（r287：上限边界 64/64 全绿，批准执行）

- 4096×1024（16 行/块恰顶 64 上限）：`fired=1 chunks=64 verified=1 bad=0/4194304`；拆模块干净，refs 回 1/1，窗口零新增 WARN。无代码改动。
- **bridge 未碰，freeze 继续。**独立模块执行侧已无未验证项（3 几何 + 3 颜色 + 5 轮 soak）。
- 遗留：合并策略 recon。本地提交仍未 push。
---


## 本轮进展（r288：合并策略 recon，离线决策）

- 决定：模块转正式工具不再为合并改动；桥 r280 fire 保留（默认关）并记流水线自阻塞 defect（合流前须串行移植）；holder 解锁程序明确（stop service，需用户协调）。纯文档，无代码改动。
- 遗留：桥 fire 串行移植 + holder 窗口（动桥前提）；快照 §5 缺陷落盘攒刷新 pass。本地提交仍未 push。
---


## 本轮进展（r289：超限 `-E2BIG` 活体拒绝，批准执行）

- 3840×2160（128 块）：`fired=0 chunks=128 result=-7`，零提交零像素触碰；拆模块干净，refs 回 1/1，窗口零新增 WARN。模块正反分支活体全覆盖，无代码改动。
- **bridge 未碰，freeze 继续。**
- 遗留：holder 窗口（UMD 驱动 fire 唯一剩余路径）。本地提交仍未 push。
---


## 本轮进展（r290：UMD 驱动 fire 首绿，批准执行）

- 桥 fire 串行移植（离线）：handler 定位+调度，work 逐块执行；门禁改判 13 项 + 反向 + 366+292 全绿 + W=1 零警告。停桌面窗口 + 三开 + 真实 blit：UMD 矩形 1280×1024 `fired=1 chunks=21 verified=1 bad=0/1310720`，STATUS #1 真实绘制打通。
- 恢复曲折：脚本 grep 缺 `-a` 误判 TIMEOUT；用户手动重开桌面致 rmmod 被拒（r291 订正：非自重启）；手动补恢复关账（probe 31→1，L3 全绿，桌面拉回）。窗口零新增 WARN。脚本两 bug 已修。
- 教训：窗口约束是用户容忍度（90 秒定律作废）；CCB nonzero=40（+1 未命名）。**Freeze 已恢复。**
- 遗留：TA/3D CCB 与 update 的 UMD 驱动验证（用户协调窗口）。本地提交仍未 push。
---


## 本轮进展（r291：UMD 驱动 fire 复现全绿 + r290 订正，批准执行）

- 第二窗口零干预全绿：UMD 矩形二次 `fired=1 chunks=21 verified=1`；L3 双绿；终态 refs 1/1，窗口零新增 WARN。CCB 第三样本 nonzero=40（轮值第 9 值 `dd 35`，+1 仍未命名）。
- r290 订正（用户指正）：两次“重启”均为手动重开，无自重启证据；90 秒定律作废，约束为用户容忍度。脚本时限收紧（60/60）+ LC_ALL/unset 修已验证生效（本轮 trace 零污染、fired 一次命中）。
- **Freeze 已恢复。**
- 遗留：TA/3D CCB 或 update 的 UMD 驱动验证（需离线 recon producer）。本地提交仍未 push。
---


## 本轮进展（r292：同寿命双发全绿，批准执行）

- 第三窗口零干预：seq=1/seq=2 背靠背 `fired=1 chunks=21 verified=1`（单发复位成立）；L3 双绿；终态 refs 1/1，窗口零新增 WARN。CCB 第四/五样本 nonzero=40（轮值第 10/11 值）。脚本多轮化（BLITS/WINID）。
- **Freeze 已恢复。**
- 遗留：TA/3D 或 update 的 UMD 驱动验证（需离线 recon producer；blit submit3 后 hanging 是前置山）。本地提交仍未 push。
---


## 本轮进展（r293：hanging 机制 recon，离线）

- submit3 后寂静 = 等完成信号（无 fence/无回写/零像素）；r279 不定论收回；r294 窗口以 wchan/stack/GDB 验 poll-vs-spin。纯文档。
- 遗留：r294 GDB 窗口（用户已授权自测）。本地提交仍未 push。
---


## 本轮进展（r294：hanging 实锤 spin + 有界自杀，批准执行）

- `=2` 纯 observe 窗口照挂；活体 wchan=0/R + GDB 栈 `SyncPrimWait→sched_yield`；约 100s 后 SIGABRT（core 入库）。L3 双绿，refs 1/1，窗口零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：SyncPrimWait 三元组抓参（r295 窗口，frame+registers）；TA/3D 仍需 producer。本地提交仍未 push。
---


## 本轮进展（r295：参数抓取未遂 + 锚点，批准执行）

- GDB 活体两次干净；反汇编得有界等待 + 32B 表形状；fabricated 真 IN 得 handle/offset；入口参未得（r296 重抓）。暂存区入库前丢失（备忘；load-bearing 已提）。L3 双绿，refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：r296 入口抓参；awaited value；TA/3D producer。本地提交仍未 push。
---


## 本轮进展（r296：入口三元组落定，批准执行）

- GDB dprintf 抓到全进程唯一 `SyncPrimWait` 调用：rdi=栈表项，rsi=100s(ns，精确等式），rdx=100000(ms)——100 秒三方闭环。恢复经一次重开挡回后二次关账；L3 双绿，refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：submit3 后桥写同步值满足 UMD（需离线改代码 + 窗口验，STATUS #2 切入点）。本地提交仍未 push。
---


## 本轮进展（r298：bump 首跑被拒 -95，批准执行）

- `=2` + bump（fire 关）窗口：submit3 即拒 `-95`（某 update 柄无 CPU 映射），UMD 即时 134（无 hang）；hang/abort 因果再证。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。r297 离线部分（bump 代码 + 7 门禁 + 373+292）已提交。
- **Freeze 已恢复。**
- 遗留：r299 逐项诊断 + 复验。本地提交仍未 push。
---


## 本轮进展（r299：bump 满足 UMD 越过 submit3，批准执行）

- 同窗口诊断（entry 1 sync=0x0 NULL 填充）→ 双遍跳过修 + 门禁 → 热换复打：`first_sync=0x1029 first_val=1`，UMD 等待即过，止于像素比对 exit=1；缺口收敛为 fire 写目的池。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。
- **Freeze 已恢复。**
- 遗留：fire-into-destination（真实绘制像素闭环）。本地提交仍未 push。
---


## 本轮进展（r301：落池首验像素仍差 + r300 设计，批准执行）

- r300 离线：fire-into-destination（work 验拷 + handler 等 fire 再 bump，60s 可中断）；门禁 +5；378+292 全绿，W=1 零警告。
- r301 活体：五开两发；首发尾块 span 修；复打执行落地（todst=1 全验）但 UMD 像素 FAIL（候选错池/stride/错色 → r302 离线判）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。
- **Freeze 已恢复。**
- 遗留：r302 池归属/stride/真色判定。本地提交仍未 push。
---


## 本轮进展（r305–r312 追认补记：活体三轮 + 离线四轮）

- 活体：r305（magic 全零）/ r307（override 生效仍 FAIL）/ r309（RED/GREEN 全灭）/ r311（形态三值；判决丢失备忘）。离线：r306（ccbref + pristine）/ r308（颜色覆盖）/ r310（distinct）/ r312（poolbox）。详见报告与索引；门禁链全绿。
---

## 本轮进展（r304：CCB 扫描离线 + r303 归属反转补记，零硬件触碰）

- r303 补记：池清单三池同尺寸 nz=0/1/2621440——fire 在填源池（0x1032），目的池（0x1019）恒零；归属反转实锤。
- r304：官方树无执行逻辑可抄（OS 胶水 + 闭二进制，RE 采矿链见报告）；CCB 目的扫描（共享头 + C 回环自测，`+40` 以执行纠正）+ locate 定向（force/启发双模）；门禁；380+299 全绿，W=1 零警告。未加载。
- 遗留：r305 定向 fire 窗口（真实绘制像素闭环候选）。本地提交仍未 push。
---


## 本轮进展（r313：图案几何落定，批准执行）

- poolbox：源池 solid 全覆盖（first=3841=HEAD，last+4=池尾-TAIL）；颜色/归属/执行皆对；只差比对输入点名（r314 GDB）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：r314 GDB 断比对循环。本地提交仍未 push。
---


## 本轮进展（r317：copy producer 侦察，批准执行）

- part 1 收官：118 提交已推；快照刷新（§1/§5/§6/§11 合并 r185–r316，指针 `d8ed054`→`115a67d` 两步推进）已二推。门禁数实测：386 Python + 299 C。
- part 2 首步：tq-perf 单发（`=2` observe）止于 `TQJobSubmit` 内 abort（101 全零；r162 fabricated 中止点活体复现；core 入库）。copy/TA 均需 producer 级 recon—— transfer-fill 之外无现成 UMD producer。
- 拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。时钟疑似跳变 3h（备忘，不影响 uptime 轴）。
- **Freeze 已恢复。**
- 遗留：大项立项（copy/TA 二选一或先还债）。本地提交仍未 push（r317 起）。
---

## 本轮进展（r316：真实绘制 Test PASS，批准执行）

- r314：GDB 断比对循环点名 dest+0 vs source+3841（rcx/rsi/r13d + 映射对照）；r315：落池基址改为 0（门禁改判，386 全绿）；r316：五开窗口 `Test PASS (exit=0)`——真实绘制全链条打通，STATUS #1 落定。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。
- **Freeze 已恢复。**
- 遗留：push 积压提交；快照刷新 pass（§12/对应提交指针落后）。本地提交仍未 push。
---


## 本轮进展（r318：copy abort RE，离线）

- core 验尸 + 反汇编：断言式自杀，setup 深水区，桥全 0 无罪；候选排序（sysmem/小几何/GDB/全反汇编）。纯文档。
- 遗留：r319 三连发窗口（sysmem/小几何/对照）。本地提交仍未 push。
---


## 本轮进展（r319：tq-perf 三连发矩阵，批准执行）

- sysmem/小几何/对照三发同形 abort（134/8200/101 全零/末 map），abort 与配置无关；反汇编定位 abort 桩与调用链。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：r320 GDB 断 abort 桩读参。已推送（本轮起恢复 push）。
---


## 本轮进展（r320：abort 机制收官，批准执行）

- GDB batch 教训（start/pending/commands 的可用子集已探明）；abort 点寄存器已破坏；栈取证：destination-magic + 0x3ff 维度对（组装期 abort）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 提议收官：push；大项（TA producer recon / copy 全反汇编）另立，需拍板。
---


## 本轮进展（r321：abort 尾跳定锤，离线）

- 全二进制 call（0 命中）/jmp（1 命中 file 0x88650→0x2c8cc）扫描定锤尾跳；解释 core 栈 + r320 未命中；helper 静态无名。纯文档。
- 遗留：r322 断 abort 调用点读参（短窗口）。本地提交未 push（攒两轮一起推）。
---


## 本轮进展（r322：abort 桩命中读参，批准执行）

- GDB 文件脚本法断调用点三命中；rdi 系堆 job 结构（非字符串）；abort 在 RGXTDMSubmit 内（尾跳）；校验条件仍未命名。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：RGXTDMSubmit 对齐反汇编找 abort 分支；或 job 结构差分。已推送（恢复 push 习惯）。
---


## 本轮进展（r324：返值抓取未遂，批准执行）

- finish 版脚本空跑（pending 未命中，9 行）；r323 收敛为“全进入”；两步走方案已定。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：r325 两步走读返值。已推送。
---

## 本轮进展（r323：setup 三元组全进入（r324 已收敛“全返回”待证），批准执行）

- BlitInit/CheckFences/LookUpEOT 全进入全返回（含参数）；abort 在下游，RGXTDMSubmit 未达；dprintf 文件脚本法定稿。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，窗口零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：LookUpEOT 返回值/出参（断点停机读 rax 或静态跟分支）。已推送。
---


## 本轮进展（r325：嵌套与扫描双空跑，批准执行）

- catch 内建断点可用，但 `finish` 嵌套静默失败；`LookUpEOT` 无 `ret`（尾跳风格）；返值改道 core 出参/行为判据。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：core 读 LookUpEOT 出参（离线，r317 core 现成）；或行为判据窗口。已推送。
---


## 本轮进展（r326：EOT 假设证伪，批准执行）

- 同窗口先 PASS blit 再 tq-perf：仍同形 abort（134/8206/101 全零/末 map）；完成态非钥匙。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：CheckFences 读写（fence 状态机嫌疑）或静态跟分支。已推送。
---


## 本轮进展（r328：字段落定 + 返回值忽略，批准执行）

- dprintf 绝对地址版命中：type=0/count=1/flags=0/a8=0 两轮一致；调用点解码证明返回值被忽略、`[r8]` 系出参（极性之争消解）。拆桥 + 默认 + L3 双绿 + 拉回；refs 1/1，零新增 WARN。无代码改动。
- **Freeze 已恢复。**
- 遗留：`[r8]` 出参槽在 abort 前的值（core 读或 GDB 断桩读调用者帧）。已推送。
---

## 本轮进展（r327：反汇编切入，批准执行）

- 真入口对齐解码 type/count 开关与 fence 对象循环；dprintf 配方首发转义漏改（修正后 r328 命中）。停桌面窗口一次；L3 双绿，零新增 WARN。
---


## 本轮进展（r329：桌面 GPU 禁用，听用户指令）

- 菜单覆盖层两份 `.desktop` 加 `--disable-gpu`（`%U` 保留），`gio launch` 验证生效；但 renderD128 照持（flag 与主进程占用无关）——bridge 重载仍需窗口。GDB 函数注入致 UI 重启事故备忘；`pgrep -x` 16 字符截断教训。
- bridge 未动，refs 1/1。无代码改动。
- 遗留：copy/TA 大项待立项。已推送。
---


## 本轮进展（r330：桌面全清，用户指令）

- exe 精确匹配清 10 进程（TERM 即净；serve 保留）；现 ref 0，天然重载窗口。无代码改动。
- 遗留：用户冷启动后 holder 即回归（菜单 flag 已就绪，仍会持有——预期内）。
---

## 本轮进展（r332：活会话重建，批准执行）

- 冷启动后重建：cold 因 `guest=0` 被拒（设备已干净，非缺口）→ fresh-trial 新 trial `20261008T025100Z-c85ff8c5`（0/0/1/1/1，固件 sha 不变）→ 默认桥 + L3 双绿（refs 1/0，零新增 WARN）。**Freeze 生效。**
- 遗留：r328 `[r8]` 出参读数（下一轮，需批准）。
---

## 本轮进展（r333：出参判决，批准执行）

- GDB 活体单发 copy tq-perf（`=2` 窗口）：CF 一次命中（0/1/0/0，rsi=ctx 反证）→ abort 点槽值仍为 1——mismatch 解读死亡，abort 在下游。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 副产 `scripts/cf-slot-window.sh` 落库；GDB `printf` 不吃 python 变量教训。`bt 8` 无输出缺口记报告。
- 遗留：LookUpEOT 后状态（release 返回值或 job 差分），另行批准。
---

## 本轮进展（r334：空表断言，批准执行）

- GDB 活体单发 copy tq-perf（`=2` 窗口）：release 内断言分支首轮即中（`ebx=0/edx=0/mem12=0`）——release 列表容量为 0，前置缺失第一条命名条件。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/release-assert-window.sh`；entry 零命中 + `bt` 两轮静默 + r322 归属收回，三事如实记。
- 遗留：计数槽谁清零/没填（写入史追踪或换 producer 差分），另行开轮。
---

## 本轮进展（r335：调用链静态闭合，离线）

- 纯 objdump/nm：release 系多入口簇，TQ 经 `+0x3320=0x889c0` 进入（`0x601dd` 直调，分发后走 `+0x2ff0` 进 abort 循环）——r334 entry 零命中得解。纠 r334 `+0x3320` 算术（`0x889c0` 非 `0x888c0`）。
- 遗留：活体一发（`+0x3320` 入口 + `0x88a15` 分发 + 计数槽写观察），另行开轮。会话未碰，freeze 继续。
---

## 本轮进展（r336：同路径无人写，批准执行）

- GDB 活体单发（`=2` 窗口）：`+0x3320` 入口锚定 + 计数槽硬件写观察零命中 + 分发 `0/1/0` 走 `+0x2ff0`——零值系调用前带来。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/dispatch-watch-window.sh`；堆地址跨轮一致备忘。
- 遗留：调用前填充责任方（写入史追踪或 fill 对照），另行开轮。
---

## 本轮进展（r337：零值胎里带来，批准执行）

- GDB 活体单发（`=2` 窗口）：四入口快照指针关联——ctx 链在 BlitInit 入口已全链接且 `cnt=0`；生产者在上游。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/stage-snapshot-window.sh`；LOOKUPEOT 标量行备忘。
- 遗留：`TQJobSubmit` 入口快照定锤（表从未被建则转立项），另行开轮。
---

## 本轮进展（r338：空壳定锤，批准执行）

- GDB 活体单发（`=2` 窗口）：JobSubmit 入口链尚空 → BlitInit 入口空壳建成（cnt 恒零）——copy 表从未被建，转立项。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/jobsubmit-snapshot-window.sh`；r333–r338 链条一句话记报告。
- 遗留：copy/TA 转 RE 立项或接受不可达，不再烧单窗口。
---

## 本轮进展（r339：ctx 来自调用方，离线）

- 纯 objdump/nm：ctx=`*(job+0x10)` 调用前已存在；序言零写 `+0x58`、无分配；建表责任在调用方。链条终版记报告。
- 遗留：活体一发（JobSubmit 入口 `bt` + job 指针倾印），另行开轮。会话未碰，freeze 继续。
---

## 本轮进展（r340：bt 静默，批准执行）

- GDB 活体单发（`=2` 窗口）：JobSubmit 入口 `bt` 同样零输出——文件脚本下 `bt` 通用不可用，改走 `x/gx $rsp`。app 进 transfer API，TQJobSubmit 内部经指针到达。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/jobsubmit-bt-window.sh`。
- 遗留：返回地址一发点名调用方，另行开轮。
---

## 本轮进展（r331：头文件拼写收尾，离线）

- r332 草稿收尾：修双 `#else`（内核构建全灭）+ 9 处裸 `pr_info` 转 `mt_gpu_vm_log`（`mt_gpu_vm.h`×6、`mt_process_resources.h`×1、`mt_boot_bo.h`×2，复用宏）；新门禁 8 项（含反向）。
- 门禁：`check-offline` 394 Python + 299 C 全绿；`make kernel` W=1 零警告；`make check` 全绿（HEAD 上原是红色，止于 bootstrap `kvzalloc`）。`verify-mmu-bootstrap.py` 全过且 validation json 零 diff。
- 教训：诊断日志误放 `/tmp/opencode/`（84K，已清，`/tmp` 仅 1%）；后续易失产物走 `build/traces/<rNN>/`。`runtime-integration-build.json` 随 L2 刷新提交（旧凭证 272→299）。
- 遗留：r328 的 `[r8]` 出参读数（需重建活会话，待批准）；`mt_boot_bo.h:118 kzalloc` 未动。
---

## 本轮进展（r341：尾跳调用方，批准执行）

- GDB 活体单发（`=2` 窗口）：返回地址归属 app `0x402b`，`QueueTransferNew+0x46` 尾跳进 JobSubmit——调用方点名，`bt` 静默根因亦明。生产者即 copy-setup 自身（`rep stos` 后无回填）。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/retaddr-window.sh`。
- 遗留：app `0x3f00–0x4030` 离线反汇编（优先）或断 `0x4026` 活体读参，另行开轮。
---

## 本轮进展（r342：app 入参活体，批准执行）

- GDB 活体单发（`=2` 窗口）：断 app `0x4026`，rdx 缓冲 `+0x820/+0x828/+0x838` 非零（栈指针），计数槽仍零；收回 r341“清零后无回填”的说法。默认回 + L3 双绿，零新增 WARN。**Freeze 已恢复。**
- 落库 `scripts/app-args-window.sh`。
- 遗留：app `0x3f00–0x4030` 离线反汇编，命名 rdx 缓冲写入来源；计数槽（ctx 链）与 rdx 缓冲的对应待确认。
---

## 本轮进展（r343：修正 r342，离线）

- 反汇编闭合：rdx 缓冲 `[0,0x820)` 由 `rep stos` 清零（`0x3ffc→0x400b`，`%r12` 自 `0x3c5c` 未改写），`+0x820` 起的非零值为残留栈，撤回 r342“app 填入”解读。计数槽仍空，生产者仍待 transfer 侧 RE（`RGXTDMQueueTransferNew` 0x614e0）。零硬件触碰，freeze 继续。
- 遗留：`RGXTDMQueueTransferNew` 参数消费（离线）。
---

## 本轮进展（r344：分发门，离线）

- 反汇编 `RGXTDMQueueTransferNew`：特性门 `>1→TQJobSubmit`（尾跳）、`≤1→legacy`；`rdx+8` 由 r342×r338 活体互证；`=2`/默认行为分裂得解。ctx 空壳随 job 传入，候选出自 `CreateTransferContext`。零硬件触碰，freeze 继续。
- 遗留：`CreateTransferContext` 建表契约 + fill 序列对照（离线）。
---

## 本轮进展（r345：app 未描述 surface，离线）

- app copy 序列调用清点：无 surface 描述调用；`CreateCCB` 只 calloc。r333–r345 因果链闭合，copy 线关账（重心回 fill 扩展或 TA，由用户拍板）。零硬件触碰，freeze 继续。
- 遗留：vendor 完整 copy 流程对照（开放项）。
---

## 本轮进展（r346：TA 阶梯，离线）

- `RGXKickTA` 入口链静态定锤；缺 producer/桥口/执行三件，阶梯 T1–T3 已列（T1 下轮）。copy 线保持关账，TA 为新执行覆盖。零硬件触碰，freeze 继续。
- 遗留：T1 `SubmitTADataEnQueue` 桥命令归属（离线）。
---

## 本轮进展（r347：T1 关闭，离线）

- `EnQueue` 纯入队不发桥命令；0x82 静态普查 18 个、无 `0xC`；T2 锁定 `0x14`。两次 `/tmp` 违规（已删）。零硬件触碰，freeze 继续。
- 遗留：T2 fabricated TA producer（r210 配方移植）。
---

## 本轮进展（r348：T2-a，离线 fabricated）

- 新脚本可复现 fabricated 双映射试探：`RGXKickTA -> 3`，`PrepareTA=0`，3 来自 `SubmitTA`；`0x82:0x14` 未发出。整形三跳收敛。
- 落库 `scripts/ta-kick-attempt1.sh`；r194“返回3”平反（系 SubmitTA 非守卫）。
- 遗留：T2-b SubmitTA 的 3 归因（`0x9c250`？）。
---

## 本轮进展（r349：T2-b，离线 fabricated）

- `SubmitTA` 内逐个被调者断点：`SyncPrimRef` 首报 3（`INVALID_PARAMS`，零 handle 被拒）；T2-c 回填 tuple。零硬件触碰，freeze 继续。
- 遗留：T2-c 真 handle 回填（脚本三行，fabricated）。
---

## 本轮进展（r350：T2-c，离线 fabricated）

- `SyncPrimRef` 要非空描述子，传入 NULL 即 3；真 handle 已落 `b10`，回填位置未中。零硬件触碰，freeze 继续。
- 脚本增量已单提交；mapB 漏 `$SYNC` 已补。
- 遗留：T2-d 描述子选中步骤。
---


## 本轮进展（r351：T2-d，离线 fabricated）

- 描述子选中步骤定位：`0x79c92: mov 0x48(%rdx),%rdi`，`rdx = rbx+208*i`，`rbx=*(*(r14+0x18)+0x30)`，`i=*(rbx+0x24)`；fabricated 下 i=0，槽0+0x48 为 NULL → `SyncPrimRef` 报 3。GDB 链式复核 match，断点单次命中。零硬件触碰，freeze 继续。
- 遗留：T2-e——`r14+0x18` 堆对象在 kick 结构体中的来源；b10 描述子填槽0+0x48 后复测。
---


## 本轮进展（r352：T2-e，离线 fabricated）

- b10 描述子直接验证：`SyncPrimRef(b10@0)` 返回 0（成功），`SyncPrimRef(NULL)` 返回 3（基线）；描述子 `+8=1` 符合要求。结合 r351 选中逻辑，回填槽 0 `+0x48` 后应成功。零硬件触碰，freeze 继续。
- `r14+0x18` 链静态定位：r14=RGXKickTA `rbp-0x170` 栈结构体，经 PrepareTA 原样传给 SubmitTA；`+0x18` 由 PrepareTA 写入。具体来源 buffer 未实测（GDB 在完整 mapA 下触发堆布局敏感 SIGSEGV，已穷尽绕行方案）。
- 遗留：T2-f（`r14+0x18` 来源实测定位，或 harness 层 poke 回填后跑完整 RGXKickTA）。
---

- b10 描述子直接验证： 返回 0（成功）， 返回 3（基线）；描述子  符合要求。结合 r351 选中逻辑，回填槽 0  后应成功。零硬件触碰，freeze 继续。
-  链静态定位：r14=RGXKickTA  栈结构体，经 PrepareTA 原样传给 SubmitTA； 由 PrepareTA 写入。具体来源 buffer 未实测（GDB 在完整 mapA 下触发堆布局敏感 SIGSEGV，已穷尽绕行方案）。
- 遗留：T2-f（ 来源实测定位，或 harness 层 poke 回填后跑完整 RGXKickTA）。
---


## 本轮进展（r353：T2-f，离线 fabricated）

- 端到端验证：在 `0x79c92` 处把 b10 真描述子 poke 进槽 0（`$rdx+0x48`，原 NULL）后，完整 `RGXKickTA` 路径上 `SyncPrimRef` 两次返回 0，`SubmitTA` 未跳错误出口。T2 系列核心问题闭合。零硬件触碰，freeze 继续。
- 新发现：`SyncPrimRef` 成功后下游在偏移 `0x929ce` 处 SIGSEGV（此前被 `→3` 挡住未到达），记为 T2-g 起点。
- 方法：GDB 从头运行 + `set disable-randomization off`（ASLR 开）绕开堆布局崩溃；`$rbx` 在 `0x79c92` 处已被改写，槽位须用 `$rdx+0x48`。另纠正：此前"nohup 触发崩溃"实为 harness 引号 bug（`'b5*+0'` 字面传参致 `b6+16=0`）。
- 遗留：T2-g（`0x929ce` SIGSEGV 定位）。
---

## 本轮进展（r354：T2-g，离线 fabricated）

- 定性：`0x929ce` 处 SIGSEGV（`mov (%rax),%edi`，`rax=fault_addr=0x6000`）是 **fabricated artifact**，非真实执行链问题。`0x6000` 经 GDB 实测溯源自 `GetSrvHandle @ 0x3c1c0`（`rdi ? *(uint64_t*)rdi : 0`）的返回值——某结构体首 qword 存的是句柄值而非指针；该值经 `0x79733 → 0x7a30b → 0x36ec0 → 0x37111 → 0x92930 (rdi=0x6000,rsi=0x82,rdx=0xc)`，在 `0x929ce` 被当作指针解引用以取 ioctl fd（`ioctl([rax], 0xc0206440=_IOWR('d',64,32), r15)`）。`0x6000` 非法指针在任何真实执行中同样会崩，而官方驱动真机正常，故真实路径下该字段必为有效指针——fabricated harness 未做完整 PVRSRV 建连/句柄表初始化所致。零硬件触碰，freeze 继续。
- 方法修正：pending 断点落在 `RGXKickTA+17` 而非入口，`pc-0x7afd0` 误算 base 差 `0x11` 致首轮行为回退到 `→3`；改由 `info proc mappings` 的 `r--p` 映射取 base 后复现成功（`SyncPrimRef → 0` ×2，再现 `0x929ce` 崩溃）。
- 遗留：fabricated TA 路径天花板已到（下游是设备 ioctl 建连路径）；下一步转向真实 DDK2 render backend（STATUS.md #1）。
---

## 本轮进展（r355：DDK2 render backend 缺口盘点，离线）

- 盘点：桥侧 dispatch 三分法——已真实实现（SRVCORE/SYNC/MM、0x88:0x4 翻译、TQX fire、MUSAKICKGFX5 schema）；accept-and-log 空桩（0x82:0x14、0x89:0xa、0x82:0x12/0x88:0x5、0x89:0x8/0x9）；缺失（0x82:0xC、TA firmware 提交通道、per-file VM/BO、UMD 真实建连）。
- 新发现：r354 证据 `0x92930(rdi=0x6000,rsi=0x82,rdx=0xc)` 表明 UMD TA 路径内实际发出 `0x82:0xC`；对照 5.2 生成头为 MUSAKICKGFX2（TA/3D/PR 提交富结构），桥侧无定义、走 `-ENOTTY`。STATUS 口径修正：0x82:0xC 从"S4 边界"升级为具体可排的下一个桥目标；0x81:0x5 维持边界。
- 需求清单 R1–R7：UMD 真实建连（`PVRSRVConnectionCreateDevice`，0xd0 结构；`GetSrvHandle @ 0x13c1c0`=`rdi?*rdi:0`，SHA 对版；`PVRSRVBridgeCall` 即 `FUN_00192930`，ioctl 号 `0xc0206440` 与桥侧 static_assert 同值）→ 0x82:0xC 实现 → 0x82:0x14 执行 → TA 提交通道 → per-file VM/BO（r208）→ DDK2 context 状态 → sync prim 导入验证。
- 分轮分解 r356+（提案）：r356=0x82:0xC wire 入库+observer 占位；r357=UMD 真实建连 recon；r358=0x82:0xC 活体观察（需批准）；r359=TA 提交通道设计；r360=0x82:0x14 执行翻译设计；r361+=实现验收。
- 遗留：r356（0x82:0xC wire 入库）。零硬件触碰，freeze 继续。
---

## 本轮进展（r356：0x82:0xC wire 入库 + observer 占位，离线）

- 入库：`kernel/mt_pvr_wire.h` 新增 `mt_pvr_musakickgfx2_in`（268B）/`_out`（12B），字段与 5.2 生成头 1:1；类型尺寸经 5.2 DKMS 包（SHA `e3f684b1…`）实证：`MTGPU_FENCE`/`MTGPU_TIMELINE`=`int32_t`，`MT_BOOL`=4B 枚举，`MT_HANDLE`=8B；268/12 与 requirements 表既有 `0x82:0xC=RGXKICKTA3D2` 条目逐字节一致。
- 占位：`pvr_cmd_musakickgfx2_observe()` 接入 dispatch，解码打印标量头字段后返 `-ENOTTY`（明确非执行；与 0x82:0x14 的 accept-and-log 区分，守 STATUS 红线）。
- 门禁：static_assert 钉尺寸+7 偏移；`test_pvr_wire_sizes.py` MAPPING/DIRECTION 新增 `(0x82,0xC)`；`check-offline` 394+299 全绿；反向验证（267→编译失败）通过后还原；`make kernel` W=1 零警告。
- 遗留：r357（UMD 真实建连 recon）。零硬件触碰，freeze 继续。
---

## 本轮进展（r357：UMD 真实建连链路 recon，离线 fabricated）

- 链路语义（语料实测，SHA `b3058c02…34237b0` 对版）：`GetSrvHandle @ 0x3c1c0`=`rdi?*rdi:0`（读连接首 qword；Ghidra 口径 `0x13c1c0`=文件偏移+`0x100000`，与r354/r355 写法统一）；连接 0xd0（`FUN_0013b7c0` 内 `PVRSRVCallocUserModeMem(0xd0)`）；首 qword 由 `OpenServicesDevice`（`FUN_00192550`）写入 0x10 services-handle 指针，其首 dword 为 DRM fd；`PVRSRVBridgeCall`（`FUN_00192930`）`ioctl(*param_1, 0xc0206440)`，`ENOTTY`→`0x26`。
- fabricated 验证（ctypes 直调真实 `.so`，7/7）：dlsym 地址约定交叉核对；`GetSrvHandle` 返回句柄指针（`0x7f…`）；NULL→0；`0x6000` 布局复现 r354 读语义；`BridgeCall(0x82,0xc)` 经 `/dev/null` fd 走 `ENOTTY` 调试路径返回 `0x26`，无崩溃（对比 r354 `0x929ce` SIGSEGV）。
- 设备打开路径盘点：`FUN_001a3ab0`→`FUN_001a4940` 扫描 render minor `0x80–0xbf`，按 driver 名 `"pvr"`/`"mtgpu"` 匹配；后接 `ioctl(0x40046445)`（SRVKM_INIT）+ `BridgeConnect`。
- r358 活体前置与验收已写出（见报告）：freeze 完好 + `renderD128` 存在 + SHA 对版；只建连不提交 GPU 工作；验收=`GetSrvHandle` 指针形态 + 桥侧 `pvr_cmd_connect` 收包 + 建连返回 0。
- 门禁：新增 `tests/test_umd_connection_layout.py`（6 项钉地址/0xd0/读语义/ioctl 号/写链），反向验证通过后还原；`check-offline` 400+299 全绿。零硬件触碰，freeze 继续。

## 本轮进展（r358：UMD 真实建连打通，真机活体）

- 建连（真机活体，纯 userspace，freeze 未碰）：`PVRSRVConnectionCreateDevice(&conn,0xffffffff,0xffffffff)`→0（`conn=0x25220fe0`，1ms），经 `_GetFd` 打开 `renderD128`（driver `pvr` 首轮匹配）→`ioctl(0x40046445)`→内部 `BridgeConnect`→`GetFeatures`（纯读）→`BridgeAlignmentCheck(1,0xa)`（桥 `pvr_stub_ok` 回零）。
- `GetSrvHandle(conn)`→`0x252211a0` 指针形态；单次显式 `PVRSRVBridgeCall(1,0,in16,out17)`→0，OUT 逐字节命中桥侧 `mt_pvr_connect_result`（`bvnc=0x0023000406600017`/`error=0`）。
- IN 布局实证（`BridgeConnect` 反汇编）：16B=`[param_3,param_5,param_4,param_2]`，取 `[0x80000850,0,0x10000,0]`；桥侧忽略 IN。
- dmesg 1117→1118 行，仅+1 行 `arena close`（fd 关闭正常清理），无 WARN/BUG/Oops；refs（bridge 0/probe 1）不变。
- 未跑 `make probe`（L3）：其 `WITH_BRIDGE` 会 rmmod，违反红线；桥未被扰动，等效健康证据为活体交互全绿。
- 门禁：`check-offline` 400+299 全绿；无代码改动。见 `reports/r358-umd-live-connect.md` + 三证据（0600）。
---

---
---
## 本轮进展（r359：0x82:0xC 活体观察停轮——在载桥无 observer）

- 基线（真机，freeze 未碰）：refs（bridge 0/probe 1）、`renderD128` 存在、dmesg 无 WARN/BUG/Oops、UMD SHA `b3058c02…34237b0` 对版、HEAD `12c6bd6` 工作区干净。
- 在载桥 build-id `2a2a…261f` ≠ 在盘 r356 构建 `0d6b…55da`（含 observer 串）；在载桥 ~11:26 加载，早于 r356 提交（14:37）约 3 小时；r356/r357/r358 均未重载桥。按任务安全协议 §2 停轮：未发 `0x82:0xC`，未重载桥。
- 门禁 `check-offline` 400+299 全绿；无代码改动。见 `reports/r359-bridge-version-blocked.md` + 证据（0600）。
---

## 本轮进展（r360：mt_pvr_bridge 重载至 r356 构建成功，真机活体）

- 换桥（真机活体，用户已批准 2026-10-08 15:13）：预检（fuser 无持有者、bridge ref 0、probe ref 1、vermagic 一致、observer 串 grep=1）→ `rmmod mt_pvr_bridge` → `insmod` r356 构建。新桥 build-id `0d6b…55da` == 在盘构建 ≠ 旧 `2a2a…261f`。
- dmesg：`unloaded cleanly` → `[drm] Initialized pvr 0.1.0 ... on minor 1` → `registered 'pvr' node`；无新增 WARN/BUG/Oops。
- 健康检查 PASS（纯 userspace，r358 方法）：`PVRSRVConnectionCreateDevice`→0、`GetSrvHandle` 指针形态、单次 `PVRSRVBridgeCall(1,0)`→0 且 OUT 逐字节命中（`bvnc=0x0023000406600017`/`error=0`）。
- 方法教训：`/proc/self/maps` 取 load bias 须减 file offset；`PVRSRVBridgeCall` 真签名 7 参数 `(handle,bridge,func,in_ptr,in_len,out_ptr,out_len)`（connect 实证 r8d=0x10/stack=0x11）。
- freeze 恢复确认：probe 仍绑 00:0e.0（ref 1，未碰）、bridge 默认参数在载（ref 0）、card0/card1/renderD128 齐全。见 `reports/r360-bridge-reload-r356.md` + 四证据（0600）。
---

## 本轮进展（r362：r361 描述子"不匹配"系 GDB 脚本读错位置）

- `CreateSyncPrim` 反汇编确认：描述子 `+0x18` = `RA_Alloc` 的 `puStack_70` 输出，写入 `*param_2`（`b10`）；r361 GDB 脚本在入口取 `$rdi`（param_1）、返回时读 `*(param_1)`——读错了位置。
- 实测（同一 harness，两次独立运行）：`*(param_2)` 处 `+0x18=<ptr>、+0x20=0`（= r352/r353）；`*(param_1)` 处 `+0x18=NULL、+0x20=<heap ptr>`（= r361 dump）。直接调用 `SyncPrimRef(*b10)` → 0。
- 定性：测量方法 bug，非输入/状态/布局问题。无需 fabricated 参数调整。r363 唯一前置：修正 GDB 脚本从 `$rsi` 取 param_2。零硬件触碰，freeze 完好。见 `reports/r362-desc-mismatch-root-cause.md` + 双证据（0600）。
---

## 本轮进展（r363：0x82:0xC 活体 IN 参数观察成功）

- r362 修正（`CreateSyncPrim` 入口捕获 `$rsi`）后 TA 路径一次打通：`SyncPrimRef` 返回 0，`0x82:0xC` 到达桥侧 observer，268B IN 解码（`kick_ta=1/kick_pr=1/kick_3d=0`，`ta_cmd_size=360`，`client_ta_upd_count=1`）并返 `-ENOTTY`；未提交 GPU 工作。
- 新发现：GDB 内直接 `open()` 的 fd 必须再做 `ioctl(0x40046445)`（INIT），否则 dispatch 卡在 `srv_handle==0` → `-ENOTCONN`，observer 永不触发。
- freeze 完好（bridge ref 0、probe ref 1），dmesg 无新增 WARN/BUG/Oops；门禁 400+299 全绿。
---

## 本轮进展（r364：TA firmware 提交通道设计）

- R4 设计：`mt_marker_ops` 新增独立 op `submit_ta_work`（与 `submit_tqx_work` 并列，不碰 TQX 路径）；TA 分配 DM3（dm=1 TQX、dm=2 3D 已占用）；firmware 命令 opcode 候选 `0x66`；`0x82:0xC` IN 解码为 `struct mt_ta_submit_params`（104B）。
- 接口头文件 `kernel/mt_ta_submit.h`（只含接口定义与静态断言，无实现逻辑）；DM/opcode 为推断、须活体验证（V1–V6 清单见报告）；`kick_pr` 语义未编造，标 TO-VALIDATE。
- 门禁：新增 `tests/test_ta_submit_layout.py`（尺寸/偏移/DM 不碰撞），反向验证通过；`check-offline` 402+299 全绿，`make kernel` W=1 零警告。
- 全程离线：未加载模块、未提交 GPU 工作、freeze 完好。见 `reports/r364-ta-submit-channel-design.md` + 盘点表证据（0600）。
---


## 本轮进展（r365：DM3 接受 opcode 0x66 marker，真机活体）

- 单发 TA marker（DM=3，opcode 0x66，空 payload）：firmware 即时消费，回 wire_id 匹配事件（words[1]=0x100，非 FAULT/超时/无视）；对照组 opcode 0x64 得标准 COMPLETE（words[1]=0）→ firmware 在分发层区分 opcode。V1（DM3）/V2（0x66）通过，无需 DM=4 回退。
- 探针 `mt_live_ta_marker.c`（一次性，未入库；build/traces/r365/，W=1 零警告）：raw queue 直发、不碰 marker store bookkeeping、事件只 peek 不 ack；两次 insmod/rmmod 均干净。
- refs 1/0 不变，dmesg 无新增 WARN/BUG/Oops；未跑 make probe（WITH_BRIDGE 会 rmmod，同 r358 取舍）。见 `reports/r365-ta-marker-dm3-opcode66-accepted.md` + 三证据（0600）。
---

## 本轮进展（r371：端到端 TA 验证被固件 trial 状态阻塞）

- 冷启动后 `0x890=2`（固件启动即为 2，非残留）；probe 改源码 3 处接受 `0x890==2`（`trial_connect=Y` 时），绑定成功。
- 但 trial 无法启动：`mt_trial_start` 要求 `0x890==0`，`connect_result=-61`，`pinned=0`；固件 MMIO `0x890=2` vs RPC `fw_state=1` 不一致。
- TA kick dispatch 到达桥侧（dmesg 解码日志），但 `pvr_session_acquire` 返 `-ENODEV`（trial 未 pinned）；r370 完成路径未被活体执行。
- 门禁 417+299 全绿，`make kernel` W=1 零警告；报告 `r371-ta-e2e-blocked-by-trial.md` 入库。


## 本轮进展（r368：wire 6 悬挂只读诊断；r367 所述 UAF 经代码证伪）

- **只读诊断**——wire 6 自 dmesg `[26595.505338]` 悬挂 ~940s 无完成事件；桥 refcnt=1（marker pinning，by design），probe ref=1；在载桥 build `4a78b331`；dmesg 零 oops/WARN。本轮零硬件碰触。
- **UAF 证伪**——TA op（`mt_marker_submit_ta_work`，r366 起未变）从未写 `m->context`（`m->context = c` 只存在于 TQX/context 两个无关 op）；`m` 为 kzalloc，`m->context` 恒为 NULL；`mt_marker_complete_ta` 的 `if (m->context)` 恒为假——**在载桥 `kfree(ctx)` 为干净释放，无 UAF 风险**（r367 系误将 TQX op 模式套用于 TA op）。
- **r367修复实引入泄漏**——去 kfree 后 `ctx` 无人接管（op 明确 "no context ownership"），每成功 dispatch 泄漏约 64B；所谓"`m->context` 未释放"不存在（恒 NULL）。
- **wire 6 根因**——生产事件路径（probe `mt_runtime_event` → 通用 `mt_marker_complete`）拒收 `0x100`；`mt_marker_complete_ta` 生产零调用（仅 r366 测试模块）；**无超时机制** → 永久悬挂。次生风险：`0x100` 事件可毒化 DM3 事件队列（drain 在 -EOPNOTSUPP 处 break 不推进 tail）。
- **恢复方案**（待确认后执行）——`mt_drain_pending.ko`（已构建，vermagic 匹配）清 wire 6 → refcnt 归 0 → `rmmod` → **回退 kfree 删除**（恢复干净释放）→ 按 r360 流程重载 → L3 复绿。r369+ 需补齐生产 TA 完成路径，否则后续 TA marker 重演悬挂。
- 见 `reports/r368-wire6-uaf-reassessment.md` + 双证据（0600）；门禁 `check-offline` 全绿。

## 本轮进展（r369：wire 6 已清除、kfree 回退上机，bridge 恢复 freeze）

- **恢复执行**——`mt_drain_pending.ko`（vermagic 匹配）insmod → `dm[3] count=1` → `drained=1 remaining_total=0`，bridge refcnt 1→0；确认后 rmmod drain 模块，无残留。
- **rmmod 桥**——回滚件 `build/traces/r369-recovery/mt_pvr_bridge.rollback-4a78b331.ko`（sha256 `1cc3d47f…`）；`rmmod mt_pvr_bridge` "unloaded cleanly"；probe ref=1 全程未动。
- **kfree 回退**——`pvr_cmd_musakickgfx2()` 成功路径恢复 `kfree(ctx)`，注释修正（r368 证伪 UAF：op 无 context ownership，释放为干净释放）；`make kernel` W=1 零警告；新桥（sha256 `4f5b08af…`）一次 insmod 成功，kallsyms 见导出，`/dev/dri/card1`+`renderD128` 正常。
- **门禁**——`check-offline` 411 Python + 299 C 全绿；新测试 `tests/test_ta_kick_ctx_release.py`（3 tests，反向验证：删 kfree→红，恢复→绿）。
- **健康**——dmesg 零 WARN/BUG/Oops；refs probe=1/bridge=0；freeze 恢复。见 `reports/r369-wire6-drained-kfree-restored.md` + 双证据（0600）。


---

## 本轮进展（r370：生产 TA 完成路径已实现，活体被 EHOSTDOWN 阻塞）

- **实现**：`mt_pvr_bridge.c` 新增 `pvr_ta_wait_complete()`（轮询 DM3 等 0x100，2s 超时）与 `pvr_ta_abandon()`（超时以 `-ETIMEDOUT` error-signal fence），接入 `pvr_cmd_musakickgfx2()` 成功路径。**不碰 frozen probe**（其 `mt_runtime_event` 为 static，`mt_marker_complete` 为旧编译副本）。
- **门禁**：`check-offline` 417 Python + 299 C 全绿；`make kernel` W=1 零警告；新增 `tests/test_ta_completion_path.py`（6 tests）+ 反向验证通过。
- **重载**：一次计划内重载成功（r360 流程），refs probe=1/bridge=0，dmesg 干净，freeze 完好。
- **阻塞**：TA kick dispatch 到达，但 `submit_ta_work` 返 `-EHOSTDOWN`——`mt_runtime_can_submit`（probe 内）拒绝，`pvr_session_acquire` 成功故 trial.pinned/connected 为真，卡点在余下条件之一。**环境/trial 状态问题，非 r370 代码所致**。新代码未被活体执行。
- 报告：`mt-vgpu-guest/reports/r370-ta-completion-path-live-blocked.md`；证据 `r370-dmesg.txt`（0600）。

## 本轮进展（r376：R5 VM 初始化重新设计，bridge 侧 proper init）

- **教训**：r375 手动拼装 `mt_gpu_vm` 致 `mt_gpu_vm_bind_many` oops；`mt_gpu_vm_init()` 要求 `page_pa==NULL`，borrow 的系统内存不满足。
- **方案**：bridge 侧 `mt_bridge_ta_vm_create()` 用合成 BO（`gpu_pa=page_to_phys()`，`page_pa==NULL`）+ 正式 `mt_gpu_vm_init()`，遵循已验证的 3D 模式（`pvr_gpu_vm_ensure`）。删除 r375 手动代码（~360 行）。
- **约束**：probe 因 trial pinned 无法重载；`mt_gpu_vm_init`/`bind_many` 均为 static inline，无跨模块问题，无需 probe API。
- **门禁**：428+299 全绿，W=1 零警告，反向验证通过。
- **活体**：bridge 已重载；V1/V2 未执行（Python harness ioctl 格式问题，非代码问题）。
- 报告：`reports/r376-bridge-proper-vm-init.md`。

## r377 (2026-10-08): Harness INIT 修复，V1/V2 活体验证通过
- r376 的 harness 因 INIT 传参错误（init_module 非 1/2）致 EINVAL；按 r373 既证格式（u32 module=2）重写后通过。
- 两次真实 TA kick：V1（mt_bridge_ta_vm_create 成功）、V2（bind_many 返回 -EINVAL，无 oops），OUT.update_fence 与 dmesg wire 精确匹配（1/1、2/2）。
- r375 的 oops 根因已消除（proper mt_gpu_vm_init + 合成 BO）。门禁 428+299 全绿。本地提交待执行。


## 本轮进展（r375：R5 基础设施实现，活体因 oops 中断）

- 实现：`mt_pvr_bridge.c` +450 行：`mt_ta_vm_context_create/destroy`（per-file 上下文）、`mt_ta_vm_map_cmd_buffer/unmap`（pin→borrow→VA）、V1/V2 钩子、`pvr_file_release` 清理。`MT_TA_VM_READY` 门保持关闭。
- 关键发现：① 跨模块 `mt_bo_vram_ops`（static const）地址不一致致 `mt_bo_system_borrow` -EINVAL，已用本地 `mt_ta_bo_borrow` 绕过；② 手动 VM 初始化不完整致 `mt_gpu_vm_bind_many` 内核 oops，已禁用 bind（V2 仅验证 pin/borrow/VA）。
- 门禁：425+299 全绿，W=1 零警告，`test_ta_vm_impl.py`（11 tests）+ 反向验证。
- 活体：一次重载后 V1 触发即 oops（D-state 进程残留，bridge ref=3 无法卸载），需重启恢复。V1/V2 未完成。
- 报告：`reports/r375-ta-vm-infra-live-interrupted.md`。

## 本轮进展（r374：R5 per-file GPU VM/BO 后端设计定稿）

- **设计**：TA 真实渲染 payload 的内存路径——per-file GPU VM（真实设备 store，非 `store=file` facade）+ `mt_bo_system_borrow()` 借入 TA 命令缓冲 + 预留 VA（`0x70000000`）绑定 + `sealed`/`uploaded` 校验门（`MT_TA_VM_READY`，照抄 TQX）。
- **映射流程 8 步**：pin userspace 页 → 构造 `mt_system_memory` → borrow 进设备 store → bind 进 per-file VM → seal/upload 页表 → `gpu_va` 填包 → 完成时 unpin+释放 BO（VM 保留复用）。
- **接口**：`kernel/mt_ta_vm.h`（`struct mt_ta_vm_context`、`struct mt_ta_cmd_mapping`、静态断言，无实现）；门禁测试 `tests/test_ta_vm_layout.py`（6 tests，反向验证通过）。
- **状态标注**：borrow/sealed 门/facade 不可用均为 [MEASURED]；VA base 与流程为 [INFERRED]；V1–V4（VA 接受性/页表正确性/双上下文隔离/关闭清理）待活体。
- **门禁**：`check-offline` 425 Python + 299 C 全绿；纯设计轮，未跑 `make kernel`（无实现代码）。
- 报告：`reports/r374-perfile-gpu-vm-design.md`；证据 `r374-inventory.txt`（0600）。

## 本轮进展（r373：OUT.update_fence 回填验证通过）

- **根因**：`OUT.update_fence` 内核回填路径一直正常——`pvr_cmd_musakickgfx2()` 的 `out.update_fence=(int)wire_id`、`pvr_out()` 的 `copy_to_user`、12B OUT 结构体（`error@0`/`update_fence@4`/`update_fence_3d@8`）与 KMD 5.2.0 生成头完全一致。r372 的 "userspace 读到 0" 系其一次性 harness 传参 bug（`out_ptr`/`out_size` 未正确设置）。
- **活体验证**（bridge 未重载）：自写 Python harness 直调 ioctl，两次 TA-only kick 均精确匹配——`OUT.update_fence=3` vs dmesg `wire=3`，`OUT.update_fence=4` vs dmesg `wire=4`。
- **修复**：`pvr_out` 失败时改记 `pr_warn`（"OUT writeback failed rc=%d wire=%u"）并返回错误码，不再先记误导性的 "submitted wire" info 日志。此前 dmesg 看似成功、userspace 实际拿错误码，正是 r372 被误导的根因。
- **门禁**：`check-offline` 425 Python + 299 C 全绿（新增 `tests/test_ta_kick_out_writeback.py` 4 tests）；`make kernel` W=1 零警告；反向验证通过（回退→3/4 红，恢复→绿）。
- 报告：`reports/r373-ta-kick-out-writeback-verified.md`；证据 `r373-live-out-writeback.txt`（0600）。

## 本轮进展（r371：端到端 TA 验证被固件 trial 状态阻塞）

- 冷启动后 `0x890=2`（固件启动即为 2，非残留）；probe 改源码 3 处接受 `0x890==2`（`trial_connect=Y` 时），绑定成功。
- 但 trial 无法启动：`mt_trial_start` 要求 `0x890==0`，`connect_result=-61`，`pinned=0`；固件 MMIO `0x890=2` vs RPC `fw_state=1` 不一致。
- TA kick dispatch 到达桥侧（dmesg 解码日志），但 `pvr_session_acquire` 返 `-ENODEV`（trial 未 pinned）；r370 完成路径未被活体执行。
- 门禁 417+299 全绿，`make kernel` W=1 零警告；报告 `r371-ta-e2e-blocked-by-trial.md` 入库。

## r378 (2026-10-08): 真实页表绑定验证通过（V2 非空，无 oops）
- 一次性内核模块 `mt_live_ta_bind`：正式 `mt_gpu_vm_init()` 创建 VM，1 真实页（pa=0x1688f8000）绑定到 VA `0x70000000`，`mt_gpu_vm_bind_many()` 返回 0，**无 oops**。
- r375 oops 根因彻底消除（proper init + ops 一致）。模块卸载干净，无泄漏；dmesg 无 WARN/BUG/Oops。
- 回归：marker 级 TA kick 正常（OUT.update_fence=3 == dmesg wire=3）。
- 诚实边界：firmware 侧 VA 翻译未验证（无查询接口）；`MT_TA_VM_READY` 门保持关闭。
- 门禁 425+299 全绿（无新增代码，仅文档证据）。本地提交待执行。



## r379 (2026-10-08): 0x82:0x14 (MUSAKICKGFX5) 调研——现状 accept-and-log，执行路径设计完成
- 桥侧 `MT_PVR_FN_RGXKICKTA3D5` → `pvr_cmd_kickta3d5_observe()`（r215），解码 108B IN 后返回 0，**未真实执行**。
- Wire 结构已入库（`mt_pvr_wire.h:311`）：108B IN（render_context + check/update 数组 + submission_va@76/size@84 + counts），4B OUT（仅 error，**无 update_fence 回填**）。
- 与 0x82:0xC 关键差异：单一 submission（vs TA+PR+3D 三分路）、显式 render_context、无 fence 回填。
- 设计：DM2（3D 引擎，推断）、`mt_marker_ops` 第 6 op `submit_3d_work`、`submission_va` 经 R5 per-file VM 映射。
- 待验证 V1–V4：DM2 接受性、firmware opcode、完成事件格式、submission 解析。
- 门禁 425+299 全绿（无新增代码）。报告 `r379-82x14-musakickgfx5-research.md`。

## r380 (2026-10-08): DM2/0x66 被 firmware 忽略，trial 会话被清除

- 单发 DM2 空 marker（opcode 0x66，r365 布局）：提交成功（wire=1），但 2 秒内无任何事件（`-ETIMEDOUT`）。Firmware 直接忽略，未返回完成/FAULT/NAK。
- **副作用**：被忽略的 marker 导致 firmware 清除 trial 会话（`0x890`: 2→0，`fw_state`: 2→0）；驱动 `trial.started` 仍为 1，形成不一致。
- 对照 r365（TA）：DM3/0x66 接受（`0x100`）、DM3/0x64 接受（标准 COMPLETE）。3D 路径行为显著不同。
- `0x66` 不是 3D 的有效 opcode（或 DM2 不接受最小 marker）。0x64 对照未测（trial 中断）。
- 安全：无 oops、无 hang；探针已卸载；bridge ref 0、probe ref 1 未动；本轮未重载模块。
- Trial 需冷重启恢复（warm reboot 不重置 firmware）。3D opcode 需从 Windows KMD 或完整 `submit_context` 路径研究，不宜在 trial 会话上试探。
- 报告 `reports/r380-dm2-opcode66-ignored.md`，门禁全绿，本地提交（未 push）。

## r381 (2026-10-08): 3D opcode 为 0x68 (RGXCompute)，0x66 在 DM2 仅对真实命令有效

**结论**：3D (DM2) 的 firmware opcode 是 **0x68** (RGXCompute, type 5)。0x66 在 DM2 上仅对真实命令包有效（mt_live_3d.c 实证，r37–r41），空 marker 被忽略（r380）。

**证据**：
- mt_work_opcode(): type 5 → 0x68 (RGXCompute), DM 2
- mt_live_3d.c: node_type=5 → DM2, req.type=3 → 0x66，真实命令成功
- Windows KMD (mtkm64.sys): 含 RGXCompute 字符串
- 完成码预测：标准 0（无 3D 特殊码定义）

**建议**：0x82:0x14 实现用 0x68；必须构造完整命令包，不得用空 marker；首次活体验证等真实 UMD 调用。

报告：mt-vgpu-guest/reports/r381-3d-opcode-0x68-rgxcompute.md


## r382 (2026-10-08): submit_3d_work 落地（第 6 op，0x68，门控关闭，离线）

**结论**：submit_3d_work 作为 mt_marker_ops 第 6 个 op 落地（仿 r366 模式），
DM2/opcode 0x68 (RGXCompute)，完成码标准 0。门控 MT_3D_SUBMIT_GATE=0 默认关闭；
0x82:0x14 dispatch 保持 r215 observer。零硬件触碰。

**实现**：
- kernel/mt_3d_submit.h (新，108 行): MT_FW_DM_3D=2, MT_FW_3D_OPCODE=0x68,
  MT_FW_3D_COMPLETE_CODE=0, MT_3D_SUBMIT_GATE=0; struct mt_3d_submit_params (56B)
  含 vm_map_hook (R5 预留); mt_3d_params_from_rgxkickta3d5() 映射函数
- kernel/mt_marker_fence.h (+163): mt_3d_work, d3_params, 第 6 op (+ABI WARNING),
  mt_fw_3d_command() (0x68@0x0c, va@0x28, size@0x30), mt_marker_submit_3d_work()
  (门控关闭→-EOPNOTSUPP; 非空校验 r380 教训), ops 表, mt_bridge_submit_3d_work 声明
- kernel/recovery/mt_pvr_bridge.c (+9): mt_bridge_submit_3d_work + EXPORT_SYMBOL_GPL

**门禁**：check-offline 430+299 全绿 (+2 新测试); make kernel W=1 零新增警告
(4 pre-existing 来自 r376); 反向验证通过 (0x99→fail, 恢复→pass)。

**诚实边界**：未活体验证；dispatch 未切换；VM 映射未实现；门控开启待 r381 TO-VALIDATE。
见 reports/r382-submit-3d-work-offline.md。

## r385 (2026-10-08): R7 Sync prim import 缺口分析（离线，零硬件触碰）

**R7 定义**（r355）：`ZeusSyncPrimImportFD` 下游在真实建连后应走通；桥侧 SYNC 命令多已实现。

**核心发现**（实测源码）：`ZeusSyncPrimImportFD` = SYNC:0xC（`BridgeSyncPrimImportFD`，IN 24B），但 Linux 桥侧**未实现**——`mt_pvr_wire.h` 无 0xC 的 `MT_PVR_FN_*` 定义，dispatch `default:` 返 `-ENOTTY`。

**SYNC 组现状**：0x0 Alloc（真实，`pvr_cmd_sync_block`）/ 0xA CpuSignal（真实，r222）/ 0x1/0x2/0x7/0x8 空桩（`pvr_stub_ok`）/ **0xC 缺失**。

**是否阻塞真实 UMD**：很可能阻塞 TA 路径——r361 证实 UMD 路径为 `...→ZeusSyncPrimImportFD→0x92930(0x82:0xC)`；`-ENOTTY` 可能致中止。但尚无活体证据（r361 在 `SyncPrimRef` 被阻塞）。

**与 R6 关系**：独立。R6=context 对象（firmware 状态），R7=sync prim 对象（FD 导入）；无依赖，但真实 UMD 渲染同时需要。

**缺口清单**：R7-1（0xC 常量定义）→ R7-2（dispatch handler）→ R7-3（IN/OUT 结构入库）→ R7-4（FD 导入语义设计）。

报告 `reports/r385-sync-prim-import-gaps.md`，证据 `reports/r385-evidence.txt`。门禁全绿。
