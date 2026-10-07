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

## 关键单篇（本轮最常用）

- 翻译器输入规约：r53（包）→ r54（PMR/VA）→ r55（对齐策略）→ r56（CCB 归属）→ r62（签发路径）。
- 运维红线：r67（泄漏）→ r68（恢复流程）。
- 门禁与复核：r58。构建 canvases：`runtime-integration-build.json`。

## 非 r 编号专题（按子系统追溯时读）

`boot-bo-lifetime`、`buffer-object-layer`、`ce-*`、`context-pool*`、
`event-fence-path`、`execution-context-path`、`firmware-*`、`gem-*`、
`gpu-vm-mapping`、`tqx-*`、`work-*` 等。完整清单见目录；
各 `*-validation.json` 为机器可读验证结果，与同名 `.md` 配对。
