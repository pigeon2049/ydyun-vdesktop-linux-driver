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

## 关键单篇（本轮最常用）

- 翻译器输入规约：r53（包）→ r54（PMR/VA）→ r55（对齐策略）→ r56（CCB 归属）→ r62（签发路径）。
- 运维红线：r67（泄漏）→ r68（恢复流程）。
- 门禁与复核：r58。构建 canvases：`runtime-integration-build.json`。

## 非 r 编号专题（按子系统追溯时读）

`boot-bo-lifetime`、`buffer-object-layer`、`ce-*`、`context-pool*`、
`event-fence-path`、`execution-context-path`、`firmware-*`、`gem-*`、
`gpu-vm-mapping`、`tqx-*`、`work-*` 等。完整清单见目录；
各 `*-validation.json` 为机器可读验证结果，与同名 `.md` 配对。
