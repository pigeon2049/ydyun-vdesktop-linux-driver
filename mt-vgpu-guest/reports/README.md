# reports 证据索引（2026-10-03）

本目录是逐轮实测证据链，文件名即编号，**不要改名**（正文与门禁引用文件名）。
找证据时先查本索引，不要逐个打开。非 r 编号的是 09-28 前后的专题记录，
只在追溯对应子系统时读。

当前主线结论见仓库根 `STATUS.md` 与 `PROGRESS-SNAPSHOT.md`。

## 主线（r23 → r71：TQX 复制 → 3D 执行 → UMD 桥 → S4-3 交接 → RGX 执行）

| 报告 | 主题 |
|---|---|
| r23–r29 | 固件连接、fence、TQX 真机复制打通与连续复制 |
| r30–r32 | 用户态复制链路、DRM/GEM/syncobj、原生矩形填充 |
| r33–r35 | Linux UMD 提交格式、1080p 大表面、Windows 图形包 C 重建 |
| r36–r38 | 渲染上下文/CSW 闭合、DM2 连通、最小 3D 工作负载 |
| r39–r41 | 106 帧批量+环回绕、用户态 DRM-3D 接口、render-target 读回 |
| r42、r44 | VA 映射上限改页表几何推导（15872）、重载后真机验证 |
| r45–r49 | PVR DMA 真机：smoke、首绑 mask 核验、handoff 验证、TQX 回读、GPU-PA 窗口转换 |
| r50–r52 | VM plan 软件侧 → 真机验证 → 新桥八级阶梯全绿 |
| r53–r54 | `0x88:0x4` 包解剖（只有同步记账）、一次 kick 的 12 PMR 全清单 |
| r55 | byte-tight 答案：round-down 会 double-map，arena + per-page 绑定 |
| r56–r57 | CCB 归属（server 侧持有；非零 CCB 待绘制路径 kick）、flags 解码 |
| r58–r59 | 全轮复核（抓到 `dma_source_release` 真 bug）、重启后验证链重放 |
| r60–r61 | file-arena 落桥、cover-page 绑定 + 独占策略实测 |
| r62–r63 | 签发包装解码（T1/T2 算法输入）、kick 只读观察落桥 |
| r64–r66 | 通道健康重验、单帧实验设计、**首次 RGX 真实执行** |
| r67–r69 | device-mutex 泄漏事件（只能重启恢复）+ 两次重启恢复 |
| r70–r71 | RGX 像素验证（64 KiB 读回）、20 帧批量像素闭环 |
| r72 | fabricated RGXKickSync 复现非零 check count（kick 结构体映射 ≥436B） |
| r73 | 非零 check kick 上真机（passthrough，T2 活体 `ufo_known=1/1`） |
| r74 | update 侧不在 RGXKickSync 路径上（证伪；DDK2 候选，`+1490` 空写） |
| r75 | DDK2 结构体全映射 + rsi 对象需求定位（崩溃精确归因，render-obj 候选） |
| r76 | ghidra 语料指路：`+0x28`=SubmissionBuf 分配器 + CCB pack 公式实测 |
| r77 | DDK2 rsi 身份落定：完整创建 kicksync 对象三槽来源（语料三角验证） |
| r78 | 活体非零 CCB create 仍走 legacy（桥故意钉特性，DDK2 需特性开关） |
| r79 | vGPU 实现路径全梳理与方向评估（方向正确，输出侧/T3 是主缺口） |
| r80 | 全景测试评估：分配→复制→渲染→执行分段盘点（L1 全绿，4 旧警告新发现） |
| r81 | mock 路径排查 + DMA 活体验收（空桩清单冻结，DMA 真映射） |
| r82 | T3 recon 第一锹：mtkm64 排除，转向 UMD PrepareTA（门控第三次出现） |
| r83 | TA 提交链到桥：0x82:0xc 全字段 + fabricated 整形（uint 换算教训） |
| r84 | SubmitTA 回填映射：fence/update 双生成器 + 同步组装（执行验证二选一） |
| r85 | 活体 KickTA：入 PrepareTA 深部，手工整形到墙（转真 GLES 绘制提案） |
| r86 | 真绘制栈 recon：GLES 无 EGL，Rogue2D 是首选 spike（同桥，96 导出） |
| r87 | Rogue2D spike：95 路调用止于 TDM 共享内存（0x89 组全表，最小实现评估） |
| r88 | 0x89 TDM 共享内存桥已实现（离线全绿，未加载，待加载窗口） |
| r89 | TDM 下游验证：Map 链全用已实现桥 + SubmitTransfer2 骨架（验收判据） |
| r90 | shim 补 0x89:0x5 非零句柄，Rogue2D 再进两桥（新卡点 import 校验） |
| r91 | import 拒绝点收敛：桥后 UMD 校验（eError 居尾排除，判别式备好） |
| r92 | 判别式 verdict：OUT 一致，嫌疑是 AcquireCPUMapping 空连接（待证伪） |
| r93 | Rogue2D 95→142：两次零句柄修复，打到 TransferContext 创建 |
| r94 | 0x89:0x0 线上成功但 CCB 自检 unwind（rogue2d 内建状态，B 路优先） |
| r95 | TransferContext 卡在 devmem 零尺寸分配（General 堆已定位） |
| r96 | TDM 控制内存 size-0 根因：计数槽空（rogue2d 栈构造） |
| r97 | P 槽位直接观测：有效但计数 0（下步硬件观察点找写入者） |
| r98 | P 是悬空指针（calloc 回收实锤）：单根因级联，只差 MapMem 一口气 |
| r99 | +0x54 从未被写入：缺前置调用序列（下步延伸序列，非整形） |
| r100 | sutu 设备选择可用；+0x54 指向 surface 创建链 |
| r101 | Layout 可执行；harness 关键字陷阱 + TestSurfaceLayout 空指针门 |
| r102 | Layout 门是值驱动（栈位全扫无效，下步枚举映射） |
| r103 | 格式表解出但 13 值全灭；门在维度/validator（下步二选一） |
| r104 | memsize 计算链定位（imul×2；sc=4 通行，三维已排除） |
| r105 | +0x54 存储点定位；Layout 不支持裸调，转 Surface 入口 |
| r106 | 依赖倒挂澄清：Surface 要 Context 堆；唯一真卡点仍是 +0x54 |
| r107 | CCB 直调链钉死 + 堆非确定方法论（出路：加载窗口或 surface 先行） |
| r108 | surface 先行证伪（零桥调用）；只剩 +0x54 写入者一个问题 |
| r109 | P+0x54 是堆越界读（usable 56）：无写入者，打法转堆喷洒 |
| r110 | 喷洒 verdict：size-0 免疫 perturb；死锁形态完整（缺第三调用） |
| r111 | chunk 解剖：读 next-chunk 数据区（内容签名；备选捷径可控分配） |
| r112 | +0x54 结构性为零：legacy-TDM 疑 vendor 死代码（收官判断，转加载窗口） |
| r113 | check-only 首帧翻译设计（空 marker + 真 fence，设计文档不写代码） |
| r114 | fence 生成器语义：同步表到三元组（输入侧链条闭环） |
| r115 | 快照刷新 pass（r72–r114 落盘，STATUS 下一步更新） |
| r116 | 4 处旧警告清零（L1 全绿，kernel 零警告，反向验证） |
| r117 | 日终活体盘点：零漂移（D 态误报教训；50 提交待 push） |
| r118 | push 队列审计：52 提交干净可推（代码/文档分离，无产物） |
| r119 | 三线收敛：SubmitTransfer 语义齐 + EGL 不通实锤（musa_dri 系桩子） |
| r120 | 加载窗口 runbook（命令级；回滚只能功能级—bit 复现已证伪） |
| r121 | r113 修正案：DM2 固定包，空即模板本体（首帧零新增代码假设成立） |
| r122 | 过期表述清扫：STATUS L1 221→226（唯一命中） |
| r123 | 意外重启后复核：树完好会话失，未重建（待批准） |
| r124 | 内核 107→111 漂移评估：在盘桥已是 111 构建，重建只差加载批准 |
| r125 | 新会话重建完成：Guest/FW 2/2，L3/L4 全绿（批准执行，会话已 freeze） |
| r126 | r113 首帧 fabricated 门禁：envelope 钉死 8 项全绿，活体执行待批 |
| r127 | 活体首帧执行成功：DM2 空包 seq=1，completed 0→1，零 fault（已卸模块） |
| r128 | RT 绑定帧成功：非零 0x45a0 被接受，completed 再 +1，零 fault（已卸模块） |
| r129 | fill smoke 红：out_syncobj=0 契约分歧（旧测试 vs 现驱动），绘制像素待定 |
| r130 | 契约裁决测试过期：修正后 smoke 全绿，新会话首个绘制像素 |
| r131 | 桥增加 `ddk_feature_set` 开关（默认 0，离线实现，零硬件触碰） |
| r132 | `ddk_feature_set=2` 活体 rung5 与默认逐调用一致（DDK2 未确认可达） |
| r133 | 非零 CCB create 在 `ddk_feature_set=2` 下仍与默认一致（门控读取位置待语料核） |
| r134 | DDK2 门控 = DRM version_major==2（语料）；r131 的 features 块开关作用点错 |
| r136 | 泄漏//tmp 排查：内核零文件写，/tmp 1%；机器已重启会话丢失；卡死候选 +26/周期、r67、Chrome 持 fd |
| r137 | +26/周期根因：sealed VM destroy 必 -EBUSY，25 backing/周期不释放（首周期 +34 含一次性 boot borrow 9；Δ1 待活体二分） |
| r138 | 会话重建+Δ1 二分：首周期+34，空载+25，带帧+26，Δ1=提交路径；桥+L3 全绿，probe ref 86 freeze |
| r139 | sealed 修复+活体验：unseal+destroy 自重开，fini/ABI 不动；空载Δ0、带帧Δ1；probe ref 2 freeze |
| r140 | Δ1 猎杀：lease_free 漏 release/drm put（兄弟实现齐全）；修后单 fill/全 smoke 均Δ0；probe ref 8 freeze |
| r141 | drm_major=2 首触 DDK2：3 新桥命令全-ENOTTY（0x82:0x12/0x88:0x5/0x2:0x2），37=stub 失败常量；对照=0 全绿；UMD 内 DDK2 桩 18 个 |
| r142 | DDK2 render 建销落地：=2 下 render 首返 0；0x2:0x2 归属 SyncPrimSet 进 stub；续堵于 CCB2；OUT 以活体 12 为准（订正 r141） |
| r143 | DDK2 CCB 建销落地：create 回 0，链进分配器后 UMD 空解引用（gdb 三帧定位）；下一轮查分配器输入 |
| r144 | 分配器输入追踪：崩溃系 harness 传参差一层间接（b5→b5* 后全绿）；0x2:0x8 定名为 SyncFreeEvent（下一轮）；机器重启会话已失 |
| r145 | 0x2:0x8 落地，DDK2 全链首绿（六符号全 0，零非零；b5* 复验成立）；新会话 ref 1/0 freeze |
| r146 | DDK2 kick 语义：零 count 同步 kick 仍走 0x88:0x4 原样受理；TA/DM 专属口是 0x82:0xC/0x81:0x5（S4）；下转向 Translator |
| r147 | check-only 首帧打通：UMD check-kick 经真实 DM2 空 marker 回 0（×3，REF 平）；修 softlockup/ops复本/自饿/RT+CSW/fence漏put 共 5 处 |
| r148 | translator teardown 活体闭环：check-only marker 后 bridge clean unload，probe ref 25→1 |
| r149 | DDK2 `drm_major=2` 下 `0x82:0x12`/`0x88:0x5`/`0x88:0x4` 全绿；check-only 经空 marker 完成 |
| r150 | TDM PMR 双对象与引用生命周期修正，TransferContext2 建销通过；零核回包定位；新单核响应待活体验证，Guest=2/FW=1 阻止安全重建 |
| r151 | mmap/arena 页表生命周期修复；补 SubmitTransfer3 108B/4B packed wire 与偏移断言；Oops 根因未定，提交仍禁用 |
| r152 | fabricated GetMultiCoreInfo 回显 caps/单核；离线门禁通过，未实跑 Rogue2D replay |
| r153 | fabricated Rogue2D Context 返回 0，95 条桥调用；Surface 未过；blit 候选无参 SIGABRT，未到 SubmitTransfer3 |
| r154 | 正确参数的离屏 blit 到达 legacy SubmitTransfer2（108B）；像素失败由 fabricated backing 限制，尚未验证 CCB |
| r155 | opt-in PMR shared backing 与 import handle 唯一化；Submit 前 TDM 区域仍全零，TQCB/heap 内容尚未定名为 CCB |
| r156 | 区分 DDK2 `SubmissionCmdGenerate`/SubmitTransfer3 与 legacy blit SubmitTransfer2；改寻可复现 DDK2 producer |
| r157 | major 2 fabricated blit 到达 SubmitTransfer3（CCB GPU VA/长度已见）；shared snapshot 排除 stale PMR 映射，CCB backing 待关联 |
| r158 | SubmitTransfer3 的 CCB VA 经 VA 台账关联到 PMR backing 并转储窗口字节（39 非零/FNV 已定，归属成立） |
| r159 | update 语义 UMD 侧确定（离线）：布局/可见性/完成条件与桥实现一致；flag&2 来源待活体；清 DBG 残留 |
| r160 | SubmitTransfer3 CCB 窗口 39B 全定位并对照生成器字段（+0x10/+0x28 吻合，+0x40 轮变待查） |
| r161 | CCB +0x40 轮变 2B 系生成器头拷贝搬运 job 计数器（活体栈确认 TQJobSubmit 路径；离线 GDB，零硬件触碰） |
| r162 | 换 producer：fill CCB 与源面数无关（机器比对仅计数器不同）；copy 路径 fabrication 下双 major 同点 abort、不可达 |
| r163 | tq-perf 同倒于 copy-setup 同一 abort 点（三重一致），非新 producer；producer 线暂止 |
| r164 | 快照刷新 pass（§5/§6/门禁计数同步；对应提交按 bA32 做法 amend） |
| r165 | 复核：门禁/重放/证据链全绿，订正 STATUS 两处过期（CCB 归属、update 语义） |
| r166 | 活体会話重建（批准执行）：Guest/FW 2/2 pinned，桥默认加载，L3 全绿，freeze |
| r167 | L4 部分通过（rung1–3 绿；rung5 阻于 UMD 侧 NULL+8，桥无罪，会话健康 freeze 继续） |
| r168 | trace 加 tid：单线程排除交错；ASLR/SMP/perturb 全排除，发布者仍未命名（批准执行，会话健康） |
| r169 | OUT 字节级对比桥无罪；6 假设全否，GDB 遮罩未解释（批准执行，会话健康） |
| r170 | GDB 监督下 L4 八级全绿（rung5–8 打通；同调用同桥，非常规但诚实） |
| r171 | update 活体注入证伪：legacy 自组包递不进，需 DDK2 重载（freeze 挡，待批） |
| r172 | DDK2 重载+全链全绿+首个真实 Submit3（同 VA/尺寸；字节未捕），桥已恢复默认 freeze |
| r173 | 门控活体兑现：`=2`→Submit3，默认→legacy 自拆；两路皆按设计拒收 |
| r174 | Submit3 accept-and-log 上线；真实 CCB 39/39 落定（仅+0x40 轮变，计数器命名收回） |
| r175 | 扩展区算术闭合：39B 逐字节溯源（entry+载荷三段），T3 输入侧可解释（零硬件触碰） |
| r176 | T3 translator 输入规约 v1（envelope/header/扩展区机/实例账/未知清单；纯文档） |
| r177 | T3 输出侧盘点：Transfer 归 TQX，颜色/几何不在 CCB 内（首要缺口）；DM 只欠 TA/3D |
| r178 | 几何/颜色通道落定：PMR 池内容 + 池代数双尺寸验证（1280×1024，行连续） |
| r179 | T3-transfer fill-input 构造器（纯函数+16 项 C 门禁+反向验证；未接活） |
| r180 | T3-transfer 活体接线规约：scratch 中转（只用已验证原语；只读 recon） |
| r181 | transfer dry-run 活体验证：程序字节与离线预言一致；附带修 fill 未初始化（批准执行） |
| r182 | TQX bring-up 打通（-22 系创建顺序）；ref 部分澄清（translator 对称，余量待查） |
| r183 | ref 离线审计：`pvr_file_release` 双 early-return 可 abandon 全部 PMR `dma_owner`（零硬件触碰；活体差分待批） |
| r184 | defaults 活体差分 Δ0：maps 无罪，+65 与 prepare/挂起强相关（kill-while-busy 精炼假设；批准执行） |
| r185 | 快照刷新 pass（r165–r184 合并；§1/§2/§5/§6/§11 同步；零硬件触碰） |
| r186 | scene 预设值抽入 `mt_addr_plan.h`（bridge+6 live 去重；287+292；反向全过） |
| r187 | 预设值复核二轮 + bridge 审计：PCI 槽位宏统一、0x88 功能号命名（290+292；反向全过） |
| r188 | 保留项全抽取：55 功能号 + PMR 尺寸 + stream/slot（292+292；反向全过） |
| r189 | 对象查找去重 + 二次审计：`pvr_object_find` 收敛 9 处（295+292；反向全过） |
| r190 | update 路径定位：非零 update 走 `0x82:0x14` RGXKickTA3D5（离线 recon + 活体计划；无代码改动） |
| r191 | kill-while-busy 关账轮：mid-maps 击杀亦 Δ0，+65 仍未命名但边界收紧（批准执行；`=2` 路被 infra 持有挡回） |
| r192 | TA producer 收敛：harness 可直调导出符号 RGXKickTA（离线 recon；psKickTA 构造待续） |
| r193 | psKickTA 构造 recon：无铸造函数，以真实 render 上下文为锚手塑（离线；手塑可在 fabricated 下做，r194 已兑现） |
| r194 | psKickTA 手塑首轮：RGXKickTA 干净返回 3（fabricated 离线；元素偏移纠偏；下步造 flag&2） |
| r195 | flag&2 手塑仍使 RGXKickTA 返回 3；调用图证实 update producer 是 RGXKickGfx 等入口，下一步转 producer 层（fabricated 离线） |
| r196 | fabricated RGXKickGfx 生成 1 项 flag&2 update 并发出 `0x82:0x14`；render slot 依赖手动 poke，真实 CCB 与桥接仍未验证 |
| r197 | 纠正 r196 字段：render-context `+0x20/+0x24` 是 PerfCount callback IDs；RGXPrepareTA 分配并初始化 update list；后证 `+0x24` 还作状态表索引 |
| r198 | 临时 musa.ini 合法设置 PerfCountEndCbID 后无 poke 越过 PrepareTA；SubmissionCmdGenerate 空首参崩溃，trace 无 `0x82:0x14` |
| r199 | 二进制指令定位 SubmissionCmdGenerate 首参为 `psKickTA+0x28` 指针目标 `+0x200` allocator；动态确认待做 |
| r200 | fabricated 创建实测 render context `+0x200` allocator 与 `+0x318` SubmissionHead 均非空；GFX 输入链待查 |
| r201 | GFX allocator 链动态闭合；update helper 生成一项 flag=2 并发出 fabricated `0x82:0x14`，清理阶段 abort 待查 |
| r202 | 排除 region descriptor 重复释放解释；r201 崩溃 free 槽属于 RGXPrepareTA update-list，heap 损坏写入者待查 |
| r203 | 修正两个 GFX 输出缓冲尺寸；watchpoint 定位 0x408 字节复制越界，fabricated `RGXKickGfx` 返回 0 |
| r204 | `0x82:0x14` UMD 发 108/4；2.7.1 结构为 96/4，Guest handler 契约未闭合，不直接复用 |
| r205 | wrapper 与 fabricated trace 对齐数组/count 偏移；尾部 12B 结构差仍未解释，不补 handler |
| r206 | 5.2 `MUSAKICKGFX5` schema 与 fabricated 108B trace 对齐，flags/VA/size/ID/count 偏移闭合；Guest handler 仍缺 |
| r207 | 固定 `0x82:0x14` 108/4 wire struct 与偏移 gate；确认当前 bridge 无底层 render context/真实 CCB 提交链 |
| r208 | 证实 PMR 页可借入设备 BO store 的基础接口；真实 backend 仍需 per-file VM/context、资源闭包与 CCB/fence 执行链 |
| r209 | shim 可选捕获 fabricated `0x82:0x14` 原始 CCB；偏移/背衬映射门禁与反向验证通过 |
| r210 | 恢复 fabricated GFX producer，首次保存 0x4700 UMD 原始 CCB 字节；check/update sync helper 均成功，执行语义仍未验证 |
| r211 | 真机重启后重建活体会話（新 trial `20261007T040408Z-f3fb55af`，Guest/FW 2/2 pinned）；桥默认加载，L3 全绿，dmesg 干净，freeze 生效 |
| r212 | 新会话 legacy check-only 复验通过（`translate_kick=1`，真实 DM2 空 marker，tag=1 fence=1）；拆桥干净、probe ref 回 1，trace 已入库 |
| r213 | 新会话 DDK2 check-only 复验通过（`=2` + `translate_kick=1`，`0x82:0x12`/`0x88:0x5`/`0x88:0x4` 全 0，fence=2）；首跑复现 r144 `b5*` 间接教训，修正后即绿 |
| r214 | 新会话第二真实绘制样本：真实 blit 单次 `0x89:0xa`，39B 中 37 与 r174 跨会话一致，仅 `+0x40` 取第三值；observe 回 0 无执行，UMD 134 系其自身路径 |
| r215 | `0x82:0x14` accept-and-log observer 落桥（离线，零硬件触碰）：108B 定界 + render 上下文鉴权 + 三重定界 + 标量上报；门禁 7 项 + fn 55→56，反向验证通过，未加载 |
| r216 | r215 新构建上机 + L3 复绿：单桥重载（probe 未碰），node/smoke 全绿，refs 1/0，dmesg 干净；observer 已在载但尚无真实流量 |
| r217 | observer 分发活体验证：raw ping `0x82:0x14` 回 `-ENOENT`（路由到达），control `0x82:0x1f` 仍 `-ENOTTY`；新 ping 工具 + 5 项门禁，会话未动 |
| r218 | observer 全路径活体验证：合法 envelope（真 context + 真 PMR 窗口，阵列全 NULL）fire 回 0，dmesg 行标量全上报；11 步全 teardown，会话未动 |
| r219 | TQX bring-up 新会话复验通过（`=2` + `translate_tqx_ctx`，`tqx-ctx: ready`，无 `-22` 回归）；probe 28→1 对称归零；附带第四个 `+0x40` 轮变值 |
| r220 | SyncPrimSet 由 stub 改真写（离线，零硬件触碰）：wrapper/生成头/活体三重互证 IN16；复用 translator 解析 + 定界写；门禁 7 项 + 改判 + MAPPING，未加载 |
| r221 | 非零 kick 活体发现：真 setter 是 `0x2:0xa`（Ghidra 伪 C 写错 fn id，objdump 实锤；r220 挂错位置下轮搬）+ translator 跳过 check 等待（`if (nupdate)` 疑笔误）；拆桥干净 |
| r222 | SyncPrimSet 搬到 `0x2:0xa` + `if (ncheck)` 修复 + 双腿全绿（预置匹配 0.045s 过 / 失配 5.007s 超时）；门禁改判 + 双重复位；值语义闭环 |
| r223 | update 写回活体验证：update-only fire 写 V=1，check kick 0.11ms 即时通过（时间即读回）；新工具 + 5 项门禁；会话未动 |
| r224 | observer 非零窗口活体验证：`0x2:0xa` 预置 5×u32 后 fire，nonzero=20/FNV/head 与离线预言逐项一致；13 步全 teardown，会话未动 |
| r225 | UMD 生成 CCB 进真桥观察：r210 字节 61 槽载入 0x4700 窗口再 fire，nonzero=107/FNV/head64 与离线预言全命中；22 步全 teardown，会话未动 |
| r226 | transfer dry-run 新会话复验通过（`=2` + `translate_transfer`，digest `fnv=0xd893618ca42d3711` 与 r181 预言逐位一致）；附带第五个 `+0x40` 轮变值 |
| r227 | 混合 kick 活体验证：预置 V7 后混合 fire（check+update）44.8ms 即过，update 写回 probe 0.11ms 即过；fence=7/8；会话未动 |
| r228 | DDK2 `SetSyncPrim` 侦察：UMD 内 SIGSEGV（RVA `0xa0b38`，param_1 形状不对），`0x2:0xd` 从未发出；非桥缺口，合法形状待 recon |
| r229 | observer 全套 DDK2 回归：`=2` 下 22 步全过，三行与 legacy 一致；dispatch 与 major 正交证实；会话未动 |
| r230 | 双开组合验证：`=2` + transfer + tqx_ctx 同轮三行同现（observe/dry-run/ready），两开关正交；附带第六个 `+0x40` 轮变值 |
| r231 | 多条目混合 kick：2 check + 1 update 同轮 44.8ms 即过，写回 probe 0.11ms 即过；工具双 PMR + 门禁更新；会话未动 |
| r232 | translator 混合 kick DDK2 回归：`=2` 下 8 步全过（45.1ms 同构），fence=11/12；translator 与 major 正交证实 |
| r233 | 多 update 条目：check=2 + update=2 同轮 45.7ms 即过，第二槽 probe 0.10ms 即过；update 循环全发布证实 |
| r234 | DDK2 check 双腿：零值 0.046s 过 / 失配 5.005s 超时；`if (ncheck)` 在 DDK2 下同样真实；双 trace 入库 |
| r235 | observer 在 translator 桥下回归：同窗口 22 步全过，CCB 行一致；开关正交证实 |
| r236 | fence fd poll 验证：混合 fire 回 0 后 poll 即时就绪（已 signaled）；新断言 + 门禁；会话未动 |
| r237 | 多文件并发验证：双 ping 并行双 PASS，dmesg 6 行齐、各自独立句柄；per-file 隔离成立 |
| r238 | 定界活体验证：超窗 `-EINVAL` + 野 index `-ERANGE`，24 项全过；安全定界真实生效 |
| r239 | 等待预算参数验证：`translate_wait_ms=1000` 下 1.008s 超时；预算成比例生效 |
| r240 | 短预算下匹配腿：预置命中 36.6ms 即过（预算只限等待）；fence=18/19 |
| r241 | 10 轮混合 soak：10/10 通过，0.1ms/轮（prepare 常驻复用，快 300 倍）；tag/fence 无跳号 |
| r242 | DDK2 短预算失配：`=2` + wait 1s 下 1.008s 后 37；major×预算双正交 |
| r243 | 短预算 UMD 全链匹配：`0x2:0xa` 预置 + kick 0.046s 即过（fence=40）；预算不影响命中路径 |
| r244 | legacy 真实 blit：止于 `0x89:0x0` → -25，UMD 中止未到 submit；trace 入库，会话未动 |
| r245 | L4 legacy 部分：compute 全过；render 路径 6 连崩 + GDB 全过（r167 翻版，桥无罪） |
| r246 | DDK2 param_1 三候选证伪（`b14*`/`b5*`/conn 全崩，同 RVA）：合法输入超 harness 能力，工具边界 |
| r247 | translator 并发冲突实锤：双混合并行一胜一败，败者 submit 环节 `-EBUSY`；附带修 poll 假阳性 |
| r248 | EBUSY 后重试：败者链单独重跑全过（0.18ms）；拒绝无副作用、可恢复 |
| r249 | observer 在 transfer 桥下回归：`=2` + transfer 下 24 步全过；开关正交补格 |
| r250 | observer 在 tqx_ctx 桥下回归：`=2` + tqx_ctx 下全过；开关正交矩阵补完 |
| r251 | UMD standalone flake 现状：render 路径 10/10 崩 + GDB 全过 + probe 偶发 2 failing（重跑即过） |
| r252 | GDB 监督 L4 闭环：rung6/rung8 全过（叠加 r245，新会话 L4 全级成立） |
| r253 | 极小预算匹配腿：wait 100ms 下 45ms 即过（fence=59/60）；预算下限安全 |
| r257 | RGXKickGfx 签名恢复（离线）：6 参数用途 + 调用链；harness 重建第一步；附 /tmp 用途调查 |
| r258 | 硬盘暂存区流程验证：AGENTS.md 改判 + `/tmp` 4.2M 清零 + 新流程 ping/rung8 全过 |
| r259 | r210 配方可复现性审计：缺 GDB 手动步骤不可直接复现；最小 GFX 命令 GDB 下返回 3（r194 复现成功） |
| r260 | ck 下 legacy blit：同样止于 `0x89:0x0` → -25；ck 不干扰 `0x89` 路径；暂存区已清空 |
| r261 | bring-up 补 pool slices（离线，零硬件触碰）：copy prepare 填 slices + 读镜像 + 先释放后销毁；门禁 6 项，未加载 |
| r262 | slices 活体未达预期：零执行零打印（已排除在载≠盘内/调用点错/dmesg 丢）；调用未到达待查 |
| r263 | slices 死锁待重启：调用可达证实后第二轮卡死 buffers->lock（D 态，rmmod 被拒）；r67 预案，待重启 |
| r264 | 重启后会话重建（r211 流程复用）：新 trial `20261007T131341Z-3b9ae877`（2/2 pinned）；默认桥 + L3 全绿，freeze 生效 |
| r265 | 锁序修复（离线，零硬件触碰）：trial_lock 分段放/取，slices 移出嵌套；门禁顺序断言；未加载 |
| r266 | slices 重验通过：锁序修复生效，`tqx slices: ready cores=1`；blit hanging 系 UMD 行为（可 rmmod，对称归零） |
| r267 | fire 函数离线实现（零硬件触碰）：scratch 8MB + locate helper + fire/submit + workqueue 回读；门禁 8 项；未加载 |
| r268 | space 上限修复（离线，零硬件触碰）：64→2112 页 + keys 栈改堆；fire 活体失败根因；门禁；未加载 |
| r269 | Guest 地址收敛（离线，零硬件触碰）：1G 配额纠正（info 解码）+ BAR2/SEG5 字面量收宏；门禁 2 项；未加载 |
| r270 | 显存误读反思：三层概念混淆 + 画像零入库；画像入快照 + AGENTS 检查单；横向排查（显示/编解码/mpc/Host版全漏） |
| r271 | DID/VID 收敛（离线，零硬件触碰）：17 文件 guard 合一 helper；宽松 5 处保留；门禁；未加载 |
| r272 | 槽位号与驱动名收敛（离线，零硬件触碰）：20+22 处合一；单次使用不碰；旧门禁改判；未加载 |
| r273 | 收敛收官审计（离线，零硬件触碰）：全仓库残留裁决，生产代码零散落；收敛工作关闭 |
| r274 | fire 活体失败：DM prepare 先倒（-22 重现，slices/fire 未达）；分项打印下轮定位 |
| r275 | fail_at 定位到 bind_boot_shared（1856 行）+ teardown WARNING 修复（未 INIT work）；待复验 |
| r276 | WARNING 修复验证：INIT 前移到 prepare 入口，失败路径 teardown 干净（零 WARNING）；bind 细分另案 |
| r277 | bind 黑盒未打开：细分打印全无新行，-22 仍在 bind_boot_shared 内；新打印行缺失未解 |
| r278 | space 缩小绕行（离线，零硬件触碰）：64 页 + 256KB scratch；Chrome 被动持有挡 rmmod，上机下轮 |
| r279 | 64 页绕行验证（批准执行）：slices ready + tqx-ctx ready 全现（Chrome 已关；dmesg 标记沿用 r278）；blit hanging 系 UMD 行为 |
| r280 | fire 分块循环离线实现（零硬件触碰）：全帧切 21 块/21 fence/验尾块；门禁 3 项 + 反向验证；未加载 |
| r281 | 分块 fire 活体被持有挡回（批准执行，未触硬件）：renderD128 被会话桌面自身持有，rmmod 被拒即停，refs 不变 |
| r282 | fire 独立模块离线实现（零硬件触碰）：自有 render 节点 + 自有 bring-up/分块 fire，直连 probe 会话，不碰 bridge；门禁 9 项 + 反向验证；未加载 |
| r283 | 独立模块首次真发射全绿（批准执行）：自有节点 bring-up + 21 块/1310720 像素逐块验过；三处活体反馈修代码；拆模块干净，窗口零新增 WARN |
| r284 | 大矩形分块通用性活体验证（批准执行）：1920×1080 绿像素 32 块全绿，与离线预言逐项一致；拆模块干净，窗口零新增 WARN |
| r285 | fire 模块 soak 重复性（批准执行）：5 轮装/打/卸全绿，ref 全对称，窗口零新增 WARN；无代码改动 |
| r286 | 当前构建+会话 legacy 基线（无重载）：真实 blit 止于 `0x89:0x0` → -25（与 r244 同形），refs 不变，窗口零新增 WARN |
| r287 | 分块上限边界活体验证（批准执行）：4096×1024 恰 64 块全绿（4194304 像素零 mismatch）；拆模块干净，窗口零新增 WARN |
| r288 | 合并策略 recon（离线决策）：保持独立 + 桥侧留 UMD 路径；桥 fire 记流水线自阻塞 defect（合流前须串行移植）；无代码改动 |
| r289 | 超限矩形 `-E2BIG` 活体拒绝（批准执行）：128 块请求零提交即拒，模块正反分支活体全覆盖；拆模块干净，窗口零新增 WARN |
| r290 | UMD 驱动 fire 首绿（批准执行）：停桌面窗口 + 三开 + 真实 blit 矩形 21 块全像素验过；恢复曲折但关账（r291 订正：用户手动重开非自重启 + 脚本两 bug 已修）；窗口零新增 WARN |
| r291 | UMD 驱动 fire 复现全绿（批准执行）：第二窗口零干预全绿（L3 双绿）；CCB 第三样本 nonzero=40（轮值第 9 值）；r290 自重启订正；脚本时限收紧 |
| r292 | 同 translator 内连续两次 UMD fire（批准执行）：seq=1/seq=2 背靠背全绿，单发复位成立；CCB 第四/五样本；窗口零新增 WARN |
| r293 | submit3 后 hanging 机制 recon（离线）：桥无 fence/无回写/零像素，UMD 等永远不来的完成信号；r279 不定论收回；r294 GDB 窗口设计 |
| r294 | hanging 实锤 SyncPrimWait 用户态 spin（批准执行）：R + wchan 0 + 活体栈 + 约 100s 有界自杀 SIGABRT；=2 纯 observe 照挂；L3 双绿，窗口零新增 WARN |
| r295 | spin 参数抓取未遂 + 同步输入锚点（批准执行）：反汇编钉死等待形状/32B 表；fabricated 真 IN 得 handle/offset；暂存区丢文件备忘（gdb-args 幸存）；L3 双绿 |
| r296 | SyncPrimWait 入口三元组落定（批准执行）：rsi==100000×1000000 精确成立，100 秒双编码铁证，全进程仅调用一次；L3 双绿，窗口零新增 WARN |
| r297 | submit3 update 回写离线实现（零硬件触碰）：opt-in bump（UMD 自值/两遍/大声失败）；门禁 7 项 + 反向；373+292 全绿，W=1 零警告；未加载 |
| r298 | bump 首跑被拒 -95（批准执行）：某 update 柄无 CPU 可见内存；hang/abort 因果模型再添一证；L3 双绿，窗口零新增 WARN |
| r299 | bump 满足 UMD 越过 submit3（批准执行）：sync=0x1029 写 1，UMD 等待即过，止于像素比对（执行缺口）；同窗口热修复；L3 双绿 |
| r300 | fire-into-destination 离线设计与实现（零硬件触碰）：work 验拷 + handler 等 fire 再 bump（60s 可中断）；门禁 +5；378+292 全绿，W=1 零警告；未加载 |
| r301 | fire-into-destination 首验（批准执行）：执行落地但像素仍差（候选：错池/stride/错色）；尾块 span 修；L3 双绿，窗口零新增 WARN |
| r302 | 池候选清单打印离线实现（零硬件触碰）：逐池 handle/bytes/pixels/nz/color + 选中行；门禁 1 项；379+292 全绿；未加载 |
| r303 | 池清单活体（批准执行）：三池同尺寸 nz=0/1/2621440，归属反转实锤（fire 在填源池）；拆桥 + L3 双绿，窗口零新增 WARN |
| r304 | CCB 目的扫描离线实现（零硬件触碰）：官方源定位（无执行逻辑可抄）+ 共享头扫描（C 自测回环）+ locate 定向；门禁；380+299 全绿；未加载 |
| r305 | CCB magic 普查活体（批准执行）：六魔数全零命中，真实布局与构建器无交集；拆桥 + L3 双绿，窗口零新增 WARN |
| r306 | VA 引用普查 + pristine 规则离线实现（零硬件触碰）：ccbref 映射 + pristine 同几何优先（颜色取源池）；门禁；383+299 全绿；未加载 |
| r307 | pristine 归属定向首验（批准执行）：override 0x1019 生效但仍 FAIL；ccbref total=10；拆桥 + L3 双绿，窗口零新增 WARN |
| r308 | 颜色覆盖参数离线实现（零硬件触碰）：translate_fire_color（0=池色）；门禁；384+299 全绿；未加载 |
| r309 | 颜色扫描双发（批准执行）：RED/GREEN 均排除（override 生效）；拆桥 + L3 双绿，窗口零新增 WARN |
| r310 | 池内容形态打印离线实现（零硬件触碰）：distinct 像素计数（solid==1）；门禁；385+299 全绿；未加载 |
| r311 | poolshape 活体读取（批准执行）：1019 全零/101b 单字节/1032 双值；blit 判决随暂存区丢失（备忘）；L3 双绿，窗口零新增 WARN |
| r312 | 池图案边界打印离线实现（零硬件触碰）：首/末非零字偏移 + 值（autorect 已回滚，读数先行）；门禁；385+299 全绿；未加载 |
| r313 | 图案几何落定 + solid 证实（批准执行）：源池 solid 全覆盖实锤；比对输入只差点名（r314 GDB 断比对循环）；L3 双绿，窗口零新增 WARN |
| r314 | 比对双方点名（批准执行）：dest+0 vs source+3841 全 5MB（rcx/rsi/r13d 活体）；L3 双绿，窗口零新增 WARN |
| r315 | 目的像素池基址离线修正（零硬件触碰）：落池基址 HEAD→0 + 门禁改判；386 全绿，W=1 零警告；未加载 |
| r316 | 真实绘制像素闭环 Test PASS（批准执行）：UMD 全链条打通（执行/落池/同步/比对）；STATUS #1 落定；L3 双绿，窗口零新增 WARN |
| r317 | copy producer 侦察（批准执行）：tq-perf 止于 TQJobSubmit 内 abort（101 调用全 0，r162 复现）；core 已入库；L3 双绿，窗口零新增 WARN |
| r318 | copy abort 根因 RE（离线）：断言式自杀（ud2+abort），setup 深水区；bridge 全 0 无罪；候选按验证成本排序；无代码改动 |
| r319 | tq-perf 三连发矩阵（批准执行）：sysmem/小几何/对照全同形 abort（与配置无关）；反汇编定位 abort 桩；L3 双绿，窗口零新增 WARN |
| r320 | abort 桩活体机制 + 栈取证（批准执行）：batch 符号教训 + abort 点寄存器已破坏；栈上 destination-magic 与维度对；L3 双绿，窗口零新增 WARN |
| r321 | abort 经尾跳进入桩（离线 RE）：全二进制 call/jmp 扫描定锤；r320 未命中解释齐；r322 断调用点设计；无代码改动 |
| r322 | abort 桩命中读参 + 结构转储（批准执行）：rdi 系堆 job 结构（非字符串）；abort 在 RGXTDMSubmit 内；L3 双绿，窗口零新增 WARN |
| r323 | setup 三元组全进入（批准执行）：BlitInit/CheckFences/LookUpEOT 入口全命中（dprintf 文件脚本法定稿）；“全返回”待返值证据（r324 收敛）；L3 双绿，窗口零新增 WARN |
| r324 | LookUpEOT 返值抓取未遂（批准执行）：finish 版脚本空跑（pending 未命中）；r323“全返回”收敛为“全进入”；两步走方案已定；L3 双绿 |
| r325 | 嵌套断点与返值扫描双空跑（批准执行）：finish 嵌套静默失败；LookUpEOT 无 ret（尾跳风格）；返值改 core 出参/行为判据；L3 双绿 |
| r326 | 先 fill 后 copy 仍 abort（批准执行）：translator 完成态不能解除 copy 中止；EOT/会话态假设证伪；L3 双绿，窗口零新增 WARN |
| r327 | CheckFences 反汇编切入（批准执行）：真入口对齐解码 type/count 开关；dprintf 配方首发转义漏改；L3 双绿，窗口零新增 WARN |
| r328 | CheckFences 字段值 + 返回值被忽略（批准执行）：type=0/count=1/flags=0/a8=0 两轮一致；r8 系出参；L3 双绿，窗口零新增 WARN |
| r329 | 桌面 GPU 禁用与菜单持久化（用户指令）：--disable-gpu 已写入菜单覆盖层并验证生效，但不释放 renderD128（两次实锤）；GDB 函数注入致 UI 重启事故备忘 |
| r256 | TQX 真发射路径盘点（离线）：bring-up 补 slices 即发射就绪；锁无障碍；三步立项 |
| r254 | 极小预算失配腿：wait 100ms 下 0.118s 后 37；预算维度全覆盖（5s/1s/100ms） |
| r255 | 短预算 10 轮 soak：10/10 通过，0.12ms/轮；预算×复用组合成立 |

## 关键单篇（本轮最常用）

- 翻译器输入规约：r53（包）→ r54（PMR/VA）→ r55（对齐策略）→ r56（CCB 归属）→ r62（签发路径）。
- 运维红线：r67（泄漏）→ r68（恢复流程）。
- 门禁与复核：r58。构建 canvases：`runtime-integration-build.json`。

## 非 r 编号专题（按子系统追溯时读）

`boot-bo-lifetime`、`buffer-object-layer`、`ce-*`、`context-pool*`、
`event-fence-path`、`execution-context-path`、`firmware-*`、`gem-*`、
`gpu-vm-mapping`、`tqx-*`、`work-*` 等。完整清单见目录；
各 `*-validation.json` 为机器可读验证结果，与同名 `.md` 配对。
