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
