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
| r353 | T2-f：回填b10真描述子到槽0后SyncPrimRef端到端返回0（两次）；SubmitTA越过检查；下游0x929ce处新SIGSEGV（离线fabricated） |
| r354 | T2-g：0x929ce处SIGSEGV定性为fabricated artifact——GetSrvHandle返回0x6000句柄值被当作指针解引用（ioctl fd获取）；修正pending断点落在RGXKickTA+17致base误算的教训（离线fabricated） |
| r355 | 真实DDK2 render backend缺口盘点：0x82:0xC(MUSAKICKGFX2)是UMD TA路径内实际发出的调用、桥侧未实现；STATUS口径修正；R1-R7需求清单与r356+分轮分解（离线盘点） |
| r356 | 0x82:0xC(MUSAKICKGFX2)wire结构入库(268/12)+dispatch接observer占位(明确非执行,返-ENOTTY)；门禁钉尺寸/偏移+反向验证；make kernel W=1零警告（离线） |
| r352 | T2-e：b10描述子直接验证通过（SyncPrimRef返回0 vs NULL返回3）；r14+0x18链静态定位到PrepareTA写入（离线fabricated） |
| r351 | T2-d：SubmitTA内描述子选中步骤定位：*(rbx+208*i+0x48)，i=*(rbx+0x24)；fabricated下i=0取NULL（离线fabricated） |
| r350 | T2-c：SyncPrimRef要非空描述子(8∈{1,2})，传入NULL；真handle已落b10，未闭合（离线fabricated） |
| r357 | UMD真实建连链路recon：GetSrvHandle读连接首qword返回有效指针（语料+SHA对版，澄清0x3c1c0/0x13c1c0为同一函数）；fabricated ctypes直调7/7走通（/dev/null fd→ENOTTY→0x26无崩溃）；设备打开路径盘点（render minor扫描+driver名pvr/mtgpu匹配）；r358活体前置与验收判据已写出（离线） |
| r358 | UMD真实建连打通（真机活体）：PVRSRVConnectionCreateDevice经renderD128建连返回0，GetSrvHandle返回指针0x252211a0，单次PVRSRVBridgeCall(1,0)返回0且OUT逐字节命中桥侧connect预期（bvnc=0x0023000406600017/error=0）；dmesg仅+1行arena close、无WARN/BUG/Oops；refs不变；freeze未碰（未跑make probe，其WITH_BRIDGE会rmmod，违反红线） |
| r359 | 0x82:0xC 活体 IN 观察停轮（真机，安全协议 §2）：在载桥 build-id `2a2a…261f` ≠ 在盘 r356 构建 `0d6b…55da`，在载桥 ~11:26 加载早于 r356 提交（14:37），不含 `pvr_cmd_musakickgfx2_observe`；未发包、未重载桥；freeze 未碰，dmesg 无新增；门禁 400+299 全绿 |
| r365 | DM3 接受 opcode 0x66 marker（真机活体）：单发空包被消费、回 wire_id 匹配事件（words[1]=0x100，非 FAULT）；对照 opcode 0x64 得标准 COMPLETE（words[1]=0），证明 firmware 区分 opcode；V1/V2 通过，无需 DM4 回退；freeze 完好 |
| r366 | submit_ta_work 落地并活体验证（真机）：mt_marker_ops 第 5 op，DM3/0x66 经桥 export 真实提交；T1 完成码 0x100+fence signal、T2 已完成依赖即满足、T3 非法 id 拒收、T4 真异步等待全绿；桥计划重载一次，probe 未动，freeze 完好 |
| r367 | 0x82:0xC 真实分发接 submit_ta_work（真机）：observer -ENOTTY 改为真实分发，IN→params 映射（D5）；TA+PR 诚实拒收（-EOPNOTSUPP）、TA-only 真实执行（wire=6）；fence 语义验证受阻——dispatch 成功路径 kfree(ctx) 致 UAF（源码已修，未上机），wire 6 未完成致桥 refcnt=1 无法二次重载；门禁全绿 |
| r368 | wire 6 悬挂只读诊断：生产完成路径缺失（0x100 被通用 matcher 拒收，mt_marker_complete_ta 生产零调用）致永久悬挂；r367 所述 UAF 经代码证伪（TA op 从未写 m->context，kfree 为干净释放，r367 "修复"实引入泄漏）；恢复方案：mt_drain_pending 清 wire 6→回退 kfree 删除→重载；门禁全绿 |
| r369 | wire 6 已清除、kfree 回退上机（真机恢复）：mt_drain_pending.ko 清 wire 6（drained=1，refcnt 1→0）→旧桥干净 rmmod（回滚件已存）→回退 r367 的 kfree 删除 （r368 证伪 UAF，恢复干净释放，消 ~64B/次泄漏）→新桥一次加载成功；dmesg 零 WARN/BUG/Oops，probe ref=1 未动、bridge ref=0，freeze 恢复；门禁 411+299 全绿 |
| r371 | 端到端 TA 验证被固件 trial 状态阻塞：冷启动后 0x890=2（固件启动即 2）；probe 改源码接受 0x890==2 后绑定成功，但 trial 无法启动（mt_trial_start 要求 0x890==0，connect=-61）；dispatch 到达桥侧但 pvr_session_acquire 返 -ENODEV（trial.pinned=0）；r370 完成路径未被活体执行；门禁 417+299 全绿 |
| r372 | Trial accepts 0x890==2, e2e TA verified: mt_trial_start relaxed (0/2), trial connected (pinned=1); two 0x82:0xC kicks OK (wire=1/2), 0x100 arrived, no hang; gate 421+299 green |
| r373 | OUT.update_fence writeback verified live: kernel path always correct (KMD header layout match); r372's zero-read was its harness passing bad out_ptr/out_size; correct harness gets update_fence==dmesg wire (3==3, 4==4); fix: pvr_out failure now pr_warn (was misleading 'submitted' info); gate 425+299 green |
| r374 | R5 per-file GPU VM/BO backend design (offline): TA cmd buffer via mt_bo_system_borrow() into per-file device-store VM; 8-step map flow; MT_TA_VM_READY gate mirrors TQX; kernel/mt_ta_vm.h + 6 layout tests; V1-V4 await live; gate 425+299 green |
| r375 | R5 TA VM infra implemented, live oops (bind disabled) (offline): TA cmd buffer via mt_bo_system_borrow() into per-file device-store VM; 8-step map flow; MT_TA_VM_READY gate mirrors TQX; kernel/mt_ta_vm.h + 6 layout tests; V1-V4 await live; gate 425+299 green |
| r376 | Bridge-side proper VM init (no probe reload): deleted r375 manual assembly (oops cause); synthetic page-table BO + mt_gpu_vm_init() per proven 3D pattern; probe pinned, no reload needed; gate 428+299 green, W=1 zero warnings; V1/V2 live pending (harness issue) |
| r377 | Harness INIT 修复，V1/V2 活体验证通过（无 oops） | r377-harness-fixed-v1v2-verified.md |
| r378 | 真实页表绑定验证通过（V2 非空，无 oops）：1 真实页绑定到 VA 0x70000000，bind_many 返回 0；VM 初始化正式，清理无泄漏；marker 回归正常；firmware VA 翻译待验证 | r378-real-bind-verified.md |
| r379 | 0x82:0x14 MUSAKICKGFX5 research: accept-and-log observer, 108B IN/4B OUT in wire.h, DM2 inferred, V1-V4 await live | r379-82x14-musakickgfx5-research.md |
| r380 | DM2/0x66 ignored by firmware (no event, timeout); trial session cleared by firmware (0x890 2->0); probe unloaded clean, no oops; trial needs cold reboot to recover | r380-dm2-opcode66-ignored.md |
| r381 | 3D opcode 为 0x68 (RGXCompute, type 5→DM2)；0x66 在 DM2 仅对真实命令有效（mt_live_3d.c 实证），空 marker 被忽略（r380）；完成码预测为标准 0；Windows KMD 确认 RGXCompute | r381-3d-opcode-0x68-rgxcompute.md |
| r382 | submit_3d_work 落地（第 6 op，0x68，门控关闭，离线）：mt_marker_ops 第 6 op，DM2/0x68，标准完成码 0；MT_3D_SUBMIT_GATE=0 默认关闭；0x82:0x14 dispatch 未切换；零硬件触碰；门禁 430+299 全绿，反向验证通过 | r382-submit-3d-work-offline.md |
| r383 | probe dead code cleanup offline: removed mt_probe_ta_vm.h (29 lines) + 122 lines in mt_guest_probe.c; bridge comment updated; kernel W=1 zero warnings; zero HW touch; gate 430+299 green | r383-probe-dead-code-removed.md |
| r384 | R6 DDK2 context statefulness 调研（离线）：0x82:0x12/0x88:0x5/0x89:0x8 皆为 handle token，无 firmware 状态；真实 context 需 11 BO+CSW+执行上下文（mt_live_3d.c 实证）；R5 per-file VM 非 per-context；marker 不阻塞、真实 UMD 渲染阻塞；缺口清单 R6-1~R6-6 | r384-ddk2-context-statefulness-gaps.md |
| r385 | R7 Sync prim import 调研（离线）：ZeusSyncPrimImportFD=SYNC:0xC 未实现（无 MT_PVR_FN 定义，dispatch 返 -ENOTTY）；SYNC 0x0/0xA 真实、0x1/0x2/0x7/0x8 空桩；UMD TA 路径含 ImportFD→0x82:0xC，很可能阻塞真实 UMD；与 R6 独立 | r385-sync-prim-import-gaps.md |
| r386 | SyncPrimImportFD 0x2:0xC implemented: KMD 5.2.0 IN 24B/OUT 12B structs, pvr_cmd_syncprim_importfd validates sync block + FD then returns current value; FD payload TO-VALIDATE; gate 438+299 green, W=1 zero warnings | r386-syncprimimportfd-implemented.md |
| r387 | R6 Route A per-context design (offline): MT_PVR_KIND_CONTEXT from token to real state—11 BOs (86KB) + CSW + exec process/ctx + per-context VM; create/destroy/kick lifecycle; R5 gradual migration (coexist then switch); R6-1~R6-6 breakdown; zero HW touch | r387-route-a-per-context-design.md |
| r388 | R6-1 per-context struct defined (offline, zero HW touch): kernel/mt_render_context.h new (struct mt_pvr_render_context 1456B: 11 BOs + exec process/ctx + 248B CSW + per-context VM ptr); mt_pvr_object += render_ctx pointer (kzalloc NULL default); 3 layout tests; gate 441+299 green, kernel W=1 zero warnings, reverse validation passed | r388-per-context-struct-defined.md |
| r389 | R6-2 Create realized + live V1 verified: mt_render_context_create() allocates 11 BOs (86,300B) + init data + binds to per-context VM (d->buffers-backed 64KB PT) + CSW + exec process/ctx (node_type=5); pvr_cmd_render2_create calls it (no longer empty token); live V1: all 11 BOs bound, READY, zero oops; gate 450+299 green, kernel W=1 zero warnings | r389-render-context-create-live-verified.md |
| r390 | R6-3 Destroy realized + live V2 verified (no leaks): mt_render_context_destroy() reverse-order teardown (exec ctx->process->11 BOs->VM); hooked into pvr_cmd_handle_release (0x82:0x13) + pvr_file_release (V4 file-close); create rollback refactored to reuse destroy; live V2a explicit destroy 25->13 refs, V2b file-close 25->13 refs, dmesg clean; gate 458+299 green, kernel W=1 zero warnings | r390-render-context-destroy-verified.md |
| r391 | R6-4 Kick-side render_ctx parsing + live V3 verified: pvr_cmd_musakickgfx2 resolves h_render_context via pvr_object_find; uses per-context VM when resources_ready, else per-file fallback (Phase 1); marker regression (no-ctx fence=1, ctx1 fence=2, ctx2 fence=3) all pass; V3 two contexts separate VMs (VM-level isolation, same vm_base_va by design); dmesg clean; gate 463+299 green, kernel W=1 zero warnings | r391-kick-render-ctx-verified.md |
| r392 | R6-5 CCB 调研结论（离线）：server 侧 CCB 不需要真实化，0x88:0x5 空 token 已足够；r144 活体证 UMD 全生命周期全 0 通过；本桥 DDK2 路径直接构造固件包（r366/r382），不经 CCB ring；0x82:0xC/0x14 IN 无 kicksync 字段；R6-5 关闭为 wont-do by design；与 R6 独立、不阻塞真实 UMD | r392-ccb-no-server-state-needed.md |
| r393 | R6-6 TDM 调研结论（离线）：server 侧 TDM context 不需要真实化，0x89:0x8 空 token 已足够；TDM=Transfer Data Manager（2D/blit）；r150 活体证 UMD 全生命周期（create→submit→destroy）全 0 通过；r174 捕获真实 CCB；submit 为 accept-and-log，无 context 状态消费者；与 R6 独立、不阻塞真实 UMD；R6-6 关闭为 wont-do by design | r393-tdm-no-server-state-needed.md |
| r394 | live 前安全测试落地（离线）：三类事故复盘→三类门禁（T1 VM 完整性扫描 / T2 (dm,opcode) 白名单 / T3 rmmod -f 黑名单）+ scripts/safe_rmmod.sh + pre-live 检查清单；反向验证 RV1–RV3 全部通过；门禁 472+299 全绿 | r394-live-safety-tests.md |
| r395 | 安全网首个实战检验（活体）：pre-live 门禁 T1/T2/T3 全绿（472+299），safe_rmmod.sh 待命；两次真实 0x82:0xC TA-only kick，OUT.update_fence=4/5 与 dmesg wire 精确匹配，零 completion timeout（0x100 到达），dmesg 零 WARN/BUG/Oops，refs 不变；零模块操作、零 rmmod -f | r395-safety-net-first-live-test.md |
| r396 | exec_ctx 接入 kick 路径设计（离线）：render_ctx 双执行上下文（exec_ctx_3d node_type=5→DM2 既有改名 + 新增 exec_ctx_ta node_type=2→DM3），共享同一 process；TA kick 传真实 exec_ctx_ta 替代 throwaway ctx；三阶段落地（加字段→替换→真实payload），marker 行为中性；完成双路径已处理 m->context；零代码改动 | r396-exec-ctx-kick-design.md |
| r397 | render_ctx 双执行上下文落地 + kick 传真实 ctx（活体）：exec_ctx 改名 exec_ctx_3d (node_type=5→DM2)，新增 exec_ctx_ta (node_type=2→DM3) + exec_ta_ready；create 7b 步建 TA ctx，destroy 先 TA 后 3D；pvr_cmd_musakickgfx2 有-context 传 &rctx->exec_ctx_ta（借用不 kfree），无-context 保留 throwaway；活体 V1 双 ctx 创建成功、marker 回归（ctx/no-ctx fence 6/7 精确匹配）、V3 双 context 隔离（fence 8/9，独立路由）；T2 新增 dual dm pinning 断言；门禁 474+299 全绿，kernel W=1 零警告 | r397-dual-exec-ctx-live-verified.md |
| r398 | R5 Phase 2 完成（离线）：删除 per-file VM 回退（file->ta_vm_ctx 字段、前向声明、create/destroy、kick fallback、mt_ta_vm.h 引入与头文件、两死亡测试）；无有效 render_ctx 时 kick 直接 -EINVAL；exec_ctx throwaway 分支删除（ctx 恒为借用，无 kfree）；4 个测试文件更新语义；门禁 474+299 全绿，kernel W=1 零警告，反向验证通过 | r398-per-file-vm-removed.md |
| r399 | R5 Phase 2 活体验证（活体）：pre-live 门禁 T1/T2/T3 全绿后重载 bridge 到 r398 构建；V1 无 ctx kick 返回 -EINVAL（errno 22，dmesg 有 r398 Phase 2 标记）；V2 有 ctx kick 成功（OUT.update_fence=10 对应 wire=10）；V3 destroy 后 probe ref 13->25->13 无泄漏；dmesg 零 WARN/BUG/Oops；门禁 474+299 全绿 | r399-perfile-removed-live-verified.md |
| r400 | 代码目录重构（离线）：Phase A 清理构建产物；Phase B tests 重组为子包（474+299 全绿）；Phase C 暂缓；Phase D 更新文档 | r400-code-restructure.md |
| r401 | 代码重构机会分析（离线）：7 个机优先级排序——P0 测试 helper 提取（47 文件重复 ROOT，2 种不一致写法）、P1 TA/3D submit 统一（~70% 重复，可提公共 ops）、P2 长函数拆分（prepare_locked 294 行、dispatch 168 行）、P3 魔法超时值命名；死代码零（r383/r398 清理彻底）；ENOTTY/EOPNOTSUPP 为有意区分非问题；kernel 头文件重组暂缓 | r401-code-refactor-opportunities.md |
| r402 | 测试 helper 提取（离线）：新建 tests/helpers.py（get_repo_root 由自身位置推导，另有 get_kernel_dir/get_scripts_dir/get_reports_dir/get_tests_dir/get_build_dir/get_kernel_header）；75 文件的 repo-root 路径表达式（ROOT/SOURCE/GUEST/TOOL/HEADER/KERNEL/SCRIPTS 等，.parents[2] 与 .parent.parent.parent 两写法）统一收敛；import 保持 discover/直接执行双模式；修复 3 文件 import sys 后置导致的 NameError；反向验证（helper 损坏→测试 FAIL）通过；门禁 474+299 全绿 | r402-test-helpers-extracted.md |
| r403 | TA/3D submit 公共 ops 框架提取（离线）：mt_marker_submit_ta_work（85 行）与 mt_marker_submit_3d_work（103 行）约 65 行重复框架逻辑合一为 mt_marker_submit_engine_work() + struct mt_marker_submit_ops（dm/validate/build/store）；差异仅 DM/门控/params 校验/构包/入库槽；3D hooks 包入 #if MT_3D_SUBMIT_GATE 保零警告；T2 白名单三测试适配 ops 表（dm 钉死+opcode 流向约束保留）；反向验证 RV1/RV2 通过；门禁 474+299 全绿，kernel W=1 零警告 | r403-ta-3d-submit-ops-framework.md |
| r404 | Dispatch 拆分（离线）：pvr_bridge_dispatch（168 行）按 bridge group 拆为 8 个 helper（srvcore/sync/mm/rgxcompute/rgxta3d/rgxkicksync/rgxhwperf/rgxtdm），主函数仅保留 ENOTCONN 检查+外层路由；pvr_translator_prepare_locked（294 行）经分析不拆（线性 6 阶段已清晰，集中 teardown，拆分会退化 fail_at 调试）；13 个文本扫描测试适配 helper 位置；反向验证通过；门禁 474+299 全绿，kernel W=1 零警告 | r404-dispatch-split.md |
| r405 | 真机适配缺口分析（离线）：定义"真实可用"四级（T0 API 通/r144 已达 → T1 GPU 真实执行/核心缺口 → T2 像素可验证 → T3 UMD 集成；T4 桌面栈 out-of-scope）；缺口清单 G1 真实 payload（P0，3–5 轮）> G3 门控（P1）> G4 回读（P2）> G2 UMD 集成 > G6 长尾 > G5 压力；路线图 r406 3D marker 活体 → r407 TA 捕获 → r408 解析 → r409 真实 TA 活体（高风险）→ r410 回读 → r411 3D 门控 → r412 UMD triangle；纯分析零硬件 | r405-real-usability-gap-analysis.md |
| r406 | 3D 包活体提交——固件无响应（忽略签名）：pre-live T1/T2/T3 全过，(2,0x68) 白名单确认；桥侧测试钩子直接调用 mt_marker_submit_engine_work()（绕过 MT_3D_SUBMIT_GATE，门控保持 0）；3D 包（opcode 0x68 @+0x0c，VA @+0x28，size @+0x30）提交成功，fence 已分配；固件 5s 内无完成事件（ETIMEDOUT），签名"submitted-but-ignored"；基础设施工作正常，固件需真实负载非 marker；测试桥仍在载（pending fence 持 ref=1），源码已恢复 r404，系统稳定 | r406-3d-live-ignored.md |
| r407 | TA 命令缓冲捕获机制验证（活体观察）：桥侧临时 hook（copy_from_user + print_hex_dump）在 0x82:0xC 入口成功捕获 TA 命令缓冲前 64 字节（ta_size=360 与 r363 一致）；捕获为 fabricated 测试模式，机制已验证；真实 UMD TA 负载布局：DM 包 80B（opcode 0x66 @+0x0c，wire_id @+0x48，pid @+0x4c）+ TA 缓冲 360B（指令流，待反汇编解析）；hook 已移除，桥重载干净构建；dmesg 无 WARN/Oops | r407-ta-payload-capture.md |
| r408 | RGXSubmitTA 不构造 TA 缓冲（离线反汇编）：调用链 RGXKickTA→RGXPrepareTA(FUN_00178800)→RGXSubmitTA(FUN_001796b0, 709 行)→BridgeRGXKickTA3D3；SubmitTA 内 0x168=360 为立即数尺寸参数、VA 取自 psKickTA[0]，全函数无缓冲写入/模板填充；PrepareTA 只做指针搬运（psKickTA[0..2]=render_ctx+0xb6/b8/ba）+状态回填；psKickTA 无 UMD 侧构造函数（r193 corroborate）；360B 内容由客户端 3D 状态机生成；r409 三条路：真实应用 trace（推荐）/KMD 文档/盲探（不推荐） | r408-tasubmitta-no-construction.md |
| r409 | 真实 TA 路径缺口确认（离线+实测）：标准程序（egltri_x11/glxinfo）实测走 Mesa llvmpipe，0 次 musakickgfx2 dispatch，不可达 DDK2 桥；离线反汇编 FUN_00169240 解析 TA 缓冲结构——40B（5 qwords）/72B（9 qwords）条目序列，render_ctx+0xb6 指针推进填充，"TA state buffer"；调用链 FUN_00169240→RGXKickTA→PrepareTA→SubmitTA（透传）；当前 mt_ta_submit_build 仅发 80B marker（ta_params 存不发）；r410 需扩展包构建器+DMA VA 映射+条目语义；门禁全绿零警告 |
r409-ta-path-gap-confirmed.md |
| r410 | Windows 驱动挖掘——TA ISA 字段语义（离线）：/opt/MTT-driver-only/ 为纯二进制（无头文件/文档），语义来自 Linux UMD 反汇编 FUN_00169240；TA 缓冲条目 40B（5 qwords）/72B（9 qwords），render_ctx+0xb6 指针推进；Q0 地址/标志、Q1 param_1+0x10、Q2 打包维度 (w-1)<<0x29\|(h-1)<<0x1a、Q3 lVar29+8、Q4 维度乘积；复杂条目 Q5-Q8 为 scissor/viewport；RGXPrepareTA 回读验证布局；Linux 桥缺 360B VA/size/DMA 映射/条目构造；真实 TA 包前置 4 项；门禁待跑 | r410-windows-ta-isa.md |
| r411 | 真实 TA 包构造基础设施（离线）：新 kernel/mt_ta_real.h（MT_TA_REAL_PACKET 门控默认 0、MT_TA_CMD_BUFFER_BYTES=0x168、40B 条目结构、mt_ta_entry_simple_build Q2 打包）；mt_marker_fence.h 集成 mt_fw_ta_real_command（#if 门控，VA @+0x28/size @+0x30 系 3D 类比推断 TO-VALIDATE）；mt_ta_submit_build 门控分发；新 tests/ta/test_ta_real.py（6 tests）；360B DMA/VA 映射未做（独立前置）；门禁 480+299 全绿，kernel 零警告，反向验证通过 |
r411-ta-real-packet-infra.md |
| r412 | 真实 TA 活体实现完成、trial 阻塞：测试钩子 pvr_cmd_ta_real_test（桥 0x82:0xFE，未提交；360B→BO[10]@4096→真实 TA 路径→等固件完成）；门控 MT_TA_REAL_PACKET=1 测试构建；用户态 ta_real_test3（INIT→Connect→Create→Test）；活体被 trial 未建立阻塞（pvr_session_acquire NULL；原版桥同样失败，非本轮所致）；实现编译零警告；待 trial 恢复后重跑 | r412-real-ta-blocked-trial.md |

| r414 | 真实 TA 首次活体执行成功（最高风险轮）：补加 `case 0xFE` 分发（r404 后空白字符 exact-match）+ `pvr_cmd_ta_real_test` 钩子（r412 hook.c 原样）；`make kernel` W=1 零警告；pre-live T1/T2/T3 全过；活体单发 `status=0`——固件 `COMPLETED (0x100)`，提交到完成 219µs；DM 布局验证：VA @+0x28/size @+0x30 由 [INFERRED] 转 [MEASURED]；40B 条目（64×64 dummy）被固件接受；`safe_rmmod.sh` 干净卸载，无 oops/WARN/hang；测试钩子未提交 |
r414-real-ta-first-live-success.md |
| r416 | T2 回读验证设计与实现（离线）：第 12 个 context BO（64×64 RGBA8，create 时绑定、destroy 释放，VM active_uses 前）；`mt_ta_real_request.target_va`（0=无）→ TA 条目 Q0=`va\|0x48000000000`（[INFERRED]）；调试 ioctl `0x82:0xFD`（`MT_TA_READBACK_DEBUG` 门控默认 0）：submit→等 fence→`pvr_translator_bo_read()`→16KB 像素；userspace `mt-ta-readback.c`（PPM P6+像素校验）；tests/ta/test_ta_readback.py +15；门禁 503+299 全绿，kernel 零警告，反向验证通过 |
r416-t2-readback-verification.md |
| r415 | 真实 TA 路径产品化（离线）：DM 布局 VA @+0x28/size @+0x30 由 [INFERRED] 转 [MEASURED]（r414）；新 `mt_ta_submit_real()` 生产函数（`mt_pvr_bridge.c`，`#if` 门控内，参数化 `mt_ta_real_request`，异步返 fence）；`mt_ta_real_buffer_build()` 纯函数；BO[10]@4096 复用评估采用（VM 已 seal）；门控开启流程文档化（含回滚）；`0x82:0xFE` 钩子确认不在生产代码；tests/ta/test_ta_real.py +8（共 14）；门禁 488+299 全绿，kernel 零警告，反向验证通过 |
r415-ta-real-path-productized.md |
| r417 | 回读路径测试加固（离线）：tests/ta/test_ta_readback.py +19（12th BO 生命周期 6、0xFD 参数校验 6、target_va 4、像素分析 3）、tests/c/pvr_bridge_core_test.c +4 函数（+326 checks）；发现并修复两处 latent build break——0xFD dispatch case 门控补 `&& MT_TA_REAL_PACKET`（(1,0) 曾报 implicit declaration）、补 `mt_ta_submit_real` 前向声明（(1,1) 组合历史从未编译成功）；新建 userspace/ta_readback_analyze.h（像素校验可单元测试，行为锁定 r416）；tests/ta/test_ta_real.py 2 测试改锚定定义；门禁 522+625 全绿，kernel 零警告，反向验证 4 项通过 |
r417-readback-test-hardening.md |
| r418 | 双门控回读活体（最高风险轮）：`mt-ta-readback` 真机单发——路径机械打通但暴露真实 ABI bug（`mt_pvr_ta_readback_in` 内核侧未 packed，24B vs userspace 20B，`pvr_in` 报 EINVAL；r416/r417 离线测试未捕获）；修复 1 行后 0xFD 全路径执行，`mt_ta_submit_real` 成功、fence 分配，但固件 5s 超时（ETIMEDOUT，submitted-but-ignored）；r414 同结构 TA（Q0=0）219µs 完成，本轮 Q0=`va\|0x48000000000`（[INFERRED]）后超时，Q0 编码很可能不对，不做盲探；12th target BO 活体绑定确认（0x7b000000）；pending TA fence 致 bridge ref=1，`safe_rmmod.sh` 正确拒绝，未强卸，待用户冷重启；门禁 522+625 全绿，kernel 零警告 |
r418-ta-readback-live.md |
| r419 | Q0 是纯 flags、地址在 Q1（离线反汇编）：FUN_00169240:44213 初始构造 `(sVar10<<4)<<48|(1<<61)` 无地址位；:44317 Path B `uVar15|(prev&mask)|0x48000000000` 纯 flags carry；Q1 低 48 位=`*(param_1+0x10)` 才是目标地址（:44300/44321）；r418 把 VA OR 进 Q0 污染 flags 致固件超时，r414 Q0=0 则 219us 成功；修正 `mt_ta_entry_simple_set_target()`：Q0=flags only、Q1=va&0xFFFFFFFFFFFF；r417 的 C 测试 2 处断言同步修正；门禁全绿，kernel 零警告 |
r419-q0-is-flags-address-in-q1.md |
| r420 | Q0/Q1 修正测试加固 + T4 纯净性门禁（离线）：新增 21 Python 测试（test_q0_purity.py T4 门禁 3：Q0 禁 OR/address 源码扫描；test_q0_q1_bitfields.py 15：flag 位独立/低 32 位禁区/Q1 48 位 mask/三态历史；test_ta_real.py DM 布局回归 3）；修复 mt_marker_fence.h stale [INFERRED]→[MEASURED]；T4 反向验证注入污染→FAIL；门禁 543+299 全绿，kernel 零警告 |
r420-q0-q1-test-hardening-t4-gate.md |
| r421 | Q0 修正后活体（最高风险轮）：双门控测试构建（事后 revert），pre-live T1-T4 全过；冷重启后 trial 重建成功；mt-ta-readback 全链路执行，12th target BO（0x7b000000）绑定成功；0xFD 提交（Q0=0x48000000000 flags-only [MEASURED]，Q1=0x7b000000 [INFERRED]）→ fence 分配 → 固件 5s 超时（ETIMEDOUT，submitted-but-ignored）；对比 r414（Q0=0/Q1=0，219us 完成）：Q0 污染不是唯一超时原因，Q1=target_va 未能提升为 [MEASURED]；pending fence 致 bridge ref=1，safe_rmmod.sh 正确拒绝，待用户冷重启；dmesg 干净；门禁 543+630 全绿，kernel 零警告 |
r421-q0-corrected-still-timeout.md |
| r422 | TA 缓冲 Header+Entries 双区结构定位 r421 超时根因（离线反汇编）：RGXSubmitTA 从 TA_buf+0x10/0x18/0x20/0x28/0x30/0x38/0x40/0x48/0x60 回读 9 qword 到 psKickTA（decompiled.c:54365 [MEASURED]）；RGXPrepareTA 写 Header 覆盖 0x00-0x160；我方代码把 40B Entry 写在 buf+0x00，Q2/Q3/Q4 恰好覆盖 Header 的 0x10/0x18/0x20，导致固件解析垃圾 Header 挂起；r414 全零=空工作故成功；Q1=target_va 未被证伪但 Entry 位置错误是更直接原因；r423 前置：确定 Entries 真实容器（544B 缓冲？）或 Header-only 测试方案 |
r422-ta-buffer-header-vs-entries.md |
| r423 | Header-only 方案落地（离线）：mt_ta_real_buffer_build() 不再写 Entry 到 buf+0（r422 污染源），仅置 TA_buf+0x10=target_va（→psKickTA[1]，[MEASURED]），其余全零；n_entries 必须为 0（Entries 容器未知）；新增 T5 Header 完整性门禁（test_header_integrity.py 5 tests：ban Entry struct/offset 算术，要求 +0x10 设置与 n_entries==0）；C/Python 存量测试同步更新；T5 反向验证注入污染→FAIL；门禁 548+1054 全绿，kernel 零警告 |
r423-header-only-t5-gate.md |
| r424 | Header-only 边界加固 + T5 门禁增强（离线）：C 扩展 test_ta_real_buffer_build_target（w/h 0x8000 边界、target_va 非对齐/超 48 位原样存储 [TO-VALIDATE]、memset 幂等）；T5 新增 test_header_write_whitelist（buffer_build 内仅 MT_TA_BUF_HDR_TARGET_VA 可写，<0x160 其他偏移→FAIL）+ test_submit_real_no_direct_buf_writes（submit 路径禁 ta_buf[] 直写）；T5 反向验证注入污染→FAIL；门禁 550+1416 全绿，kernel 零警告 |
r424-header-only-hardening-t5-plus.md |
| r425 | Header-only 首次活体（最高风险轮）：双门控测试构建（事后 revert），pre-live T1-T5 全过；冷重启后 trial 重建成功；mt-ta-readback 全链路执行（n_entries=0 被接受，12th target BO 0x7b000000 绑定成功）；0xFD 提交（Header-only：buf+0x10=target_va，其余零）→ fence 分配 → 固件 5s 超时（ETIMEDOUT）；结论：Header-only 不充分——r422 污染是真实 bug 但非完整解释，r414 全零=无工作快路径；固件或要求 +0x10 指向 render-target 元数据结构及/或其他 Header 字段；pending fence 致 bridge ref=1，safe_rmmod.sh 正确拒绝，待用户第 5 次冷重启；dmesg 干净；门禁 550+1416 全绿，kernel 零警告 |
r425-header-only-live-still-timeout.md |
| r426 | +0x10 指向 render-target 元数据结构（离线反汇编）：FUN_00178800 完整写入清单——+0x10=*(render_ctx+idx*0xD0+0x38)（per-buffer 描述符数组，非原始像素 BO，[MEASURED]）；+0x28=*(render_ctx+0x440)、+0x30=*(render_ctx+0x448)；FUN_0017d890 的 psKickTA 构建含 magic 0x3089705f3089705f；r425 超时因 16KB BO 非有效元数据结构；真实 Header 大多数字段指向 UMD 上下文内部状态，无法从零构造；r427 前置：捕获真实 UMD 的 TA Header 回放（推荐）或逆向 render context 初始化；门禁待跑 |
r426-plus0x10-render-target-metadata.md |
| r427 | Render Target 元数据结构逆向（离线反汇编）：FUN_0017d890 的 psKickTA 完整 18-qword 布局 [MEASURED]（[1]=TA_buf+0x10=RTData entry+0x00，[4]=magic 0x3089705f3089705f，[12]=0x10）；RTData 条目 0xD0 字节字段表 [MEASURED]（+0x00/+0x08/+0x48/+0x50/+0xC8/+0x118/+0x120）；render context state 关键偏移 [MEASURED]；RGXAddRenderTarget 创建流程；结论：psKickTA[1] 指向的结构本体为固件私有，UMD 只透传 VA，布局无法从 UMD 确定——方案 B 已达边界；r428 前置：选项 A（捕获真实 UMD Header）或选项 C（3D 路径验证 T2）；门禁 550+1416 全绿 |
r427-rendertarget-metadata-reverse.md |
| r428 | 无可运行 Linux vguest UMD（全 deb/src 取证：mtgpu=固件+dkms 源码、mtml 仅 libmtml.so）；mtdxum64.dll 实证为 DX10/11 UMD（导出 OpenAdapter/OpenAdapter10_2/MtDxExtGetInterfaceImpl）；Wine/VM 活体捕获不可行；选项 A 落点为静态提取：0x168 在 mtdxum64.dll 出现 151 次（行为锚点），RGXAddRenderTargetDDK2 分配 MLIST/RgnHeader 固件可见结构；r429 工作包：定位 TA Header builder + 对比 RGXPrepareTA + KMD 侧逻辑 |
r428-umd-env-ta-header-capture.md |
| r429 | Windows TA kick extract (offline disasm): mtdxum64.dll (DX10/11 UMD) builds 0x78B kick entries via FUN_180224220/FUN_1802411e0, magic 0x3089705f3089705f at [1] (Linux UMD at [4]), via D3DDDIEscapeCb; 0x168 151 hits are vtable/object-size noise, no Windows TA Header alloc; 360B Header is Linux-UMD-specific; firmware accepts multiple formats; r430: Linux RGXAddRenderTargetDDK2 MLIST/RgnHeader layout |
| r429-windows-kick-vs-linux-header.md |
| r430 | TA Header +0x10 = RgnHeader device VA, 3-hop chain measured (offline disasm): RGXAddRenderTarget:49312 local_5b0[1]=RgnHeader VA -> SetupRTDataSet:48867 RTData entry+0x00 -> RGXPrepareTA:52144 TA_buf+0x10; RgnHeader size=numRT*round_up(tiles*0x40,64) (0x100B for 64x64), UMD pre-fills 0xFFFFFFFF via InitRegionHeaderBuffer; MLIST size=numRT*0x4a000 (firmware-written); CORRECTS r428: RGXAddRenderTargetDDK2 is linux-legacy-umd-5.2.0/decompiled.c:50203, not mtdxum64.dll (MLIST/RgnHeader strings only in Linux UMD corpus); minimal valid TA Header table; r431 prereqs |
| r430-rgnheader-va-at-plus0x10.md |
| r431 | RgnHeader 13th BO implemented (offline): 0x100B for 64x64 pre-filled 0xFFFFFFFF ([MEASURED] r430 InitRegionHeaderBuffer), bound at VA slot 12 (0x7c000000); TA Header +0x10 now = RgnHeader VA (was raw pixel BO, r425 timeout cause); mt_ta_rgnheader_size() formula round_up(tiles*0x40,64) with C tests (64x64->0x100, 128x128->0x400, 65x65->0x240); struct grows 1720->1824 (layout test updated); 0x82:0xFD requires rgnheader_ready; gate 550+1490 green, kernel W=1 zero warnings |
| r431-rgnheader-alloc-init.md |
| r432 | RgnHeader live (highest-risk): dual-gate build, T1-T5 pass, trial rebuilt post-5th-cold-reboot; 13th RgnHeader BO bound va=0x7c000000 (0xFF fill), TA Header +0x10=RgnHeader VA, n_entries=0; firmware 5s timeout (-ETIMEDOUT) — RgnHeader necessary but not sufficient; +0x28/+0x30 still [UNKNOWN]; safe_rmmod correctly refused (ref=1); dmesg clean; gate 554+1490 green |
| r432-rgnheader-live-still-timeout.md |
| r433 | RgnHeader fill is 0x00000001 not 0xFFFFFFFF (offline disasm, CORRECTS r430/r431): InitRegionHeaderBuffer writes dword value 1 per dword [MEASURED]; r431 memset(0xFF) wrong, r434 must fix to dword-fill 1; +0x28/+0x30 chain fully traced (TA_state+0x1cc/+0x1ce <- RTDataSet+0x440/+0x448 <- local_5b0+0x68/+0x80), terminal [UNKNOWN] (Ghidra bounds), MLIST VA leading candidate; MLIST (0x4a000B, firmware-written) not in kick path; Mcg per-dword patching single-RT N/A; gate green |
| r433-rgnheader-fill-is-one-plus28-30-chain.md |
| r434 | RgnHeader fill corrected to per-dword 0x00000001 (offline, implements r433): MT_TA_RGNHEADER_INIT_DWORD 0xFFFFFFFFU->0x1U, memset(0xFF)->dword loop in mt_render_context_create (reuses u32 i), comments updated; C test_ta_rgnheader_init_pattern simulates dword-1 + asserts !=0xFFFFFFFF; Python test_rgnheader_init_all_ones updated (assertIn dword loop, assertNotIn memset 0xFF); reverse verify inject 0xFF->FAIL then green; gate 554+1491 green, kernel W=1 zero warnings |
| r434-rgnheader-fill-dword-1.md |
| r435 | 第 6 次冷重启未发生（bridge ref=1，r432 残留会话），停止活体：启动 ~16:20:46（dmesg -T 反推），r432 活体 17:45:43 在启动之后，无重启；bridge ref=1（r432 pending fence）；残留 render context（11 BOs+12th+13th rgnheader，READY）未 teardown；未构建双门控/未重载/未跑 readback；门禁 554+1491 全绿，kernel W=1 零警告；零硬件触碰 |
r435-cold-reboot-not-done-stop.md |
| r436 | RgnHeader fill-1 live (highest-risk, 6th cold reboot): dual-gate build, T1-T5 pass; trial rebuild needed runtime_context=1 (clean trial pinned=0 fails pvr_session_acquire which requires pinned+connected; reloaded probe, pinned=1 connected=1 Guest/FW 2/2); 13th RgnHeader BO bound va=0x7c000000 (per-dword fill 1, r434), TA Header +0x10=RgnHeader VA, n_entries=0; firmware 5s timeout (-ETIMEDOUT, errno=110) -- fill correction (0xFF->1) falsified as root cause; +0x28/+0x30 still [UNKNOWN]; safe_rmmod correctly refused (ref=1); dmesg clean; gate 554+1491 green; awaiting 7th cold reboot |
| r436-rgnheader-fill1-live-timeout.md |
| r437 | +0x28/+0x30 单 RT 恒为 0 [MEASURED]：local_5b0+0x68/+0x80 == 栈局部 local_548/local_530，RGXAddRenderTarget:49349-49350 赋 0，全函数无其他赋值点；我方置零与 UMD 一致，超时与此无关；MLIST VA（local_558）分配后零读取，不进 kick 路径——r433 候选证伪；psKickTA[3]/[10]=0；r438 前置：+0x68 布尔、RGXPrepareTA 完整写入对照、RgnHeader 内容、DM 包 |
| r437-plus28-30-are-zero-mlist-not-in-kick.md |
| r438 | +0x68 布尔 + 单 RT 完整写入对照（离线反汇编）[MEASURED]：+0x68=(uint)((*psKickTA&3)==3)，flags 由 DDK 层设置、取值 UNKNOWN；单 RT 下 UMD 必写 +0x50/+0x58（FUN_00184220 tile 打包 ((psKickTA[3 or 4]+0x3f>>6)&0x3f)<<48）、+0x68、+0x120（flags 位打包）、+0x138-+0x160（feature 条件），+0x78 起多 RT 块跳过；我方 Header-only 仅写 +0x10，其余 0——+0x50/+0x58 为 P0 新嫌疑（0 tiles 或致固件挂起）；r439 前置：实现 tile 打包+T5 白名单同步 |
| r438-plus68-tile-packing-suspects.md |
| r439 | TA Header +0x50/+0x58 tile packing implemented (offline, r438 P0): mt_ta_tile_pack(x)=(((x+0x3f)>>6)&0x3f)<<48 [MEASURED] (FUN_00184220:58215); buffer_build writes tile-packed w/h to +0x50/+0x58 (w/h approx psKickTA[3]/[4] [INFERRED]); 64x64->0x0001000000000000; doc stale 0xFFFFFFFF fill note corrected to per-dword 0x1; T5 whitelist extended to {+0x10,+0x50,+0x58} + 3 new Python tests; new C test_ta_tile_pack (10 checks, incl 4096->0 6-bit wrap) + buffer_build test rewritten (dynamic 1491->783, coverage preserved); reverse verify tile=0->precise FAIL; gate 557+783 green, kernel W=1 zero warnings |
| r439-tile-packing-plus50-58.md |
| r440 | Tile 打包活体仍超时（+0x50/+0x58=0x0001000000000000 证伪 0-tiles 假说）：第 7 次冷重启后双门控活体，RgnHeader 填 1 + tile 打包，固件仍 5s 超时；当前 Header vs UMD 必写清单仅剩 +0x120/+0x138-+0x160 未填；桥 ref=1 待第 8 次冷重启；门禁 557+783 全绿，kernel 零警告 |
| r440-tile-packing-live-still-timeout.md |

