# MEMORY-HISTORY-2026-10-03（只读归档）

> 2026-10-03 从 `MEMORY.md` 按清理规则移入（只留最新两节），原样未改。

---

## 本次会话进展（r45–r71：S4-3 交接真机落地 + 首次 RGX 执行；设计路线修正）

### 路线修正（推翻 bA39/bA40 的符号表方案，只增记不回改旧节）

- `__symbol_get()` 在本内核上不解析（实测连 printk 都返回 NULL），
  bA39/bA40 的 `EXPORT_SYMBOL` + 版本化 ops 表路线作废：
  桥改 **PCI 查找 + 驱动名校验 + drvdata + `try_module_get()` +
  核心 `dma_map_page()` 直连**；GPU PTE 输入从 Guest system-memory
  窗口翻译，**绝不从 `dma_addr` 假设**。
  `mt_pvr_session.h` 只剩 `mt_pvr_dma_page` 形状（`dma_addr` 仅供 unmap，
  `gpu_pa` 供页表），无函数表、无版本号；
  probe 不再 EXPORT 任何会话符号。
  `test_pvr_session_ops.py` 重写为 bind-path prereqs
  （40 位 mask + 无符号机制断言）。
- 提交 `da3df8b`（r45–r71 全量）已推送；门禁 221 Python
  （+arena/kick-inspect）、268 C RAM、`W=1`、ABI 全绿。

### DMA 真机（r45–r49）

- r45：无引用 bridge 热换后 PVR system-PMR DMA smoke；
  r46：重启后新 probe 首绑，mask 核验 40；
  r47：新 retained trial 上 handoff 验证（未提交 GPU 工作）；
  r48：TQX 从 DMA-backed 页回读尝试；
  r49：GPU-PA 窗口转换 + TQX DMA 回读成功
  （source IOVA→GPU PA，bias `0x8800000000` 一致）。

### VM plan（r50–r52、r55、r60–r61）

- r50 软件侧：`dma_addr`（仅供 unmap）与 `gpu_pa`（供页表）分离，
  32-table-page CPU-only plan，未对齐/超范围不断线只降级、不伪造绑定；
  r51 真机验证；r52 新桥上 UMD 八级阶梯全绿（`c77ad92d…`）。
- r55：naive round-down 被证伪——相邻 byte-tight 两项取整后共享
  `0x8000010000` 页，第二绑被 `bind_many` 以 `-EEXIST` 拒绝；
  强压即 double-map，故答案是 arena + per-page 绑定。
- r60：file-arena backing 落桥（每文件 2 MiB lazy arena，
  `fallbacks=0`）；r61：cover-page 绑定 + 先占独占
  （`pvr_cover_probe` 13 项：先映射的 A 进 plan，
  B 在 `bind_many` 内整批拒绝，无 WARN）。

### Kick 只读观察（r53–r54、r56–r57、r62–r63；零 GPU 执行）

- r53：`0x88:0x4` 84 字节解剖——只有同步记账、无 GPU 命令字节；
  合成路径 `checkFD` 是 UMD 未初始化垃圾（两次运行不同，不断言）。
- r54：一次 kick 背后 12 PMR / 295 KiB 全清单，仅 3 个 4 KiB 对齐项
  能进当时 plan，其余 byte-tight 降级（`-EOPNOTSUPP` 已钉门禁）。
- r56：CCB 由 server 侧持有（rung6 无增发 PMR/heap 证据）；
  合成路径 `0x88:0x0` CCB size 为 0——非零 CCB 内容只能来自
  走完整绘制路径的 kick，本轮 ladder 给不出（缺的唯一一块）。
- r57：flags 解码对照 2.7.1 头逐位钉住
  （`0x333`/`0x1233`/`0x303`，GPU incoherent；`alloc_flags` 已记录，
  Stage 1 忽略供未来 PTE 策略）。
- r62：签发包装解码（`RGXKickSync → 0x7272000 → ioctl`，
  6 寄存器 + 7 栈参数拼 84 字节；T1 拷贝数组 + T2 UFO→GPU PA 算法可写，
  T3 缺 DM 队列格式）；r63：T1+T2 只读观察落桥
  （在载构建 `894faf50`，`pvr_kick_probe` 非零包 `ufo_known=2/3`）。

### 首次 RGX 真实执行（r64–r66、r70–r71）

- r64：通道健康 + DMA 回读重验（marker DM1 + DMA-source TQX
  `verified=1` + BAR 回读一致）；r65：单帧实验设计
  （复用 `mt_live_3d`，全新会话硬性前置——retained sealed VM 下
  `live_3d` 类必被 `-EBUSY` 拒绝）。
- r66：新会话单帧 DM2，`completed=1 result=0`，零 fault，
  sealed 3D VM 留存（fence 证据，非像素）。
- r70：`live_3d_drm` 上 render-target 64 KiB 读回核验通过
  （bridge `renderD128` 无 MT ioctl 属预期，切 `renderD129` 通过）。
- r71：20 帧批量全 `[OK]`（seq 3..22 连续 + render-target seq 23，
  21 次真实执行零 fault）；未越 64-slot 回绕边界（沿用 r39），
  未测多进程并发，像素断言仍是 0x5a 预填 + 读回一致。

### 事故与复核

- r67：device-mutex owner-death 泄漏（MapPMR 永久挂起、两个 D 任务、
  hung_task 刷屏；锁属 PCI core，bridge rmmod 解不掉，**唯一恢复是重启**；
  教训：`timeout` 不得落在 bridge ioctl 临界区，DMA 路径改 120s 超时包装
  只做挂起探测，超时即停手）。
- r68/r69：重启后恢复流程复走（cold-disconnect → fresh-trial →
  bridge 加载 → smoke/探针/八级阶梯全绿）。
- r58：全轮复核抓到 1 真 bug（`dma_source_release()` 用 `page_pa`
  做 `dma_unmap_page`，init-失败路径泄漏，已修）+ 注释/清理；
  r59：重启后新 bridge（`e812d938`）验证链重放。

### 活态（2026-10-03 只读复核，未触碰）

- `Guest/FW 2/2 pinned`，`pending=0/completed=23`，D 态 0；
  `objects=34`/2 地址空间/2 上下文留存；`mt_pvr_bridge` 引用 0，
  `mt_live_3d_drm` 留存（sealed 空间不可卸载）；
  `/dev/dri` 有 `card1`/`renderD128` + `card2`/`renderD129`。
- **对象存储已满**：需空存储的实验（含再次的 `live_3d`）会被拒绝；
  bridge PVR PMR 路径是独立对象域，不受影响。
  不要卸载模块、解绑设备或提交额外工作。

---

## 本次会话进展（2026-10-03 文档复核与收敛）

- 复核：12 个引用路径全存在；在载 bridge build-id `894faf50`… 与快照一致；
  L1 重跑 221 Python + 268 C 全绿；活会话零变化
  （Guest/FW 2/2，pending=0/completed=23，引用 38/0）。
- 收敛两处：① 五份入口的裁决口径互相矛盾 → 统一为 STATUS → 快照 → MEMORY；
  ② 快照哈希的 amend 死循环 → 内容基线口径（只在改动快照内容时推进）。
- §7 容量表 10-03 重测（build 5.9G、reports 442 文件、tests 52C+31py、
  recovery 171 文件/54 源码）；ANALYSIS 陈年相对路径订正；
  PROTOCOL/FIRMWARE 抽 stub 入日期归档；AGENTS 检查单 +USB 短页同步项。
- “8 个符号”计数与快照 12 行阶梯表口径不一致 → STATUS 与目录入口改称“全链路”，不再计数。

---

## 本次会话进展（r72：fabricated 非零 kick count 复现；零硬件触碰）

- STATUS 下一步 1 前半闭环：512B 手工结构体驱动 `RGXKickSync`，
  fabricated 重放 6/6 发出 `0x88:0x4 check=1 update=0`；
  gdb（`UMD_TRAP="136:4"`）验证数组内容即注入值（sync 句柄 `0x600c` + fence 值 1）。
  证据：`mt-vgpu-guest/reports/r72-kick-nonzero-fabricated.md` + `r72-kick-nonzero-check1.jsonl`。
- 结构体映射（反汇编实测）：u32 count @`0xd8`（上限 12 条展开）；
  条目 `{u64 @0xe0+i*0x10, u32 @0xe8+i*0x10}`；第二个条数 u32 @`0x1b0`
  （update 侧数组偏移未定位，下一步）；结构体下限 **436 字节**，
  rung8 的 224B 只是零值下恰好不炸（`0x1b0` 越界读落新鲜零页）。
- 附带：fabricated 重放约 1/4 概率在 `RGXCreateRenderContext` 段错误，
  重试即过（纯用户态堆垃圾敏感，无硬件影响；coredumpctl 有记录）。
- 活会话零变化（Guest 引用 38，bridge 0，`card1/renderD128` + `card2/renderD129`；
  快照 §12 无需改）；真机抓包仍冻结（对象存储满 + 需单独批准）。
- 遗留：update 侧数组偏移定位；门禁加"结构体下限 436B"断言（等 update 侧一起落）；
  快照 §§1–11 仍停 bA43、刷新 pass 待攒（见归档尾）。

---

## 本次会话进展（r73：非零 check kick 上真机；单次 live 实验）

- 用户本轮明确批准真机测试：passthrough rung8 + 512B 手工结构体，
  一次即成，活桥 `0x88:0x4 ioctl_real ret=0 check=1 update=0`；
  dmesg `ufo_known=1/1`（真实 bridge sync PMR 句柄命中）——T2 活体验证。
  证据：`mt-vgpu-guest/reports/r73-live-nonzero-kick.md` + `r73-live-nonzero-kick.jsonl`。
- 无 GPU 执行，会后状态一字不差（pending=0/completed=23，引用 38/0，
  objects=34，D 态 0，无新增 WARN/BUG/Oops）；`make umd/probe` 仍禁用
  （首步 rmmod），只手跑 harness、无 timeout 包裹。
- 遗留：update 侧数组偏移定位；真实 CCB 内容仍需绘制路径；
  快照刷新 pass 待攒。

---

## 本次会话进展（r74：update 侧不在 RGXKickSync 路径上；离线证伪）

- `0x1b0=1`、`a3` 结构体各 4/4 fabricated 验证：`update` 恒为 0；
  静态穷举确认 b26 只有一个条数（`0xd8`）——update 数组输入在本路径无来源。
- 新候选 `RGXKickSyncDDK2`：同构循环但 `{u64,u64}` 条目 + `0x20` 步长；
  rung8 形状参数在其 `+1490`（`mov %rax,0x48(%rdx)`，rdx=NULL）6/6 定崩，
  需解完整入参（下一轮），勿在 b26/a3 上继续穷举。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r74-update-side-ddk2.md` +
  `r74-update-side-negative.jsonl`。
- 遗留：DDK2 入参 shaping；真实 CCB 内容仍需绘制路径；快照刷新 pass 待攒。

---

## 本次会话进展（r75：DDK2 结构体全映射 + rsi 需求定位；离线）

- DDK2 第 3 参数全映射：update（`0x0` 条数 + `@0x8+i*16` u64/u64 条目）
  + check（`0xd8` 条数 + `@0xe0+i*16`）+ 转运指针（`0xc8/0xd0`）+
  server 统一数组（check slot0–11，分隔 12，update 13 起）。
- `+1490` 崩溃精确归因：`rdx=[rsi+0x28]=NULL`（自我修正 r74 的误读）；
  rsi 须是富对象，首位候选 render 客户端对象（`0x330`）。
  `RGXCreateKickSyncContext` 已证伪（即 CCB 包装）。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r75-ddk2-struct-map.md`
  （本轮崩溃 trace 无桥流量，未归档 jsonl）。
- 遗留：render-obj 喂 DDK2 rsi 验证；真实 CCB 内容仍需绘制路径。

---

## 本次会话进展（r76：ghidra 语料指路 DDK2；离线）

- 用户指路 `/opt/MTT-driver-only/` + `ghidra-projects/`：语料
  `decompiled/linux-legacy-umd-5.2.0/`（SHA 与在用 UMD 一致）成首选 RE 路径，
  伪 C 纠正 r74/r75 两处误读；以后 RE 结论先过语料。
- DDK2 `+0x28` = `SubmissionBufAlloctorCreate` 在 create 期填入
  （门控：features>=2 + SyncPrim 两步；`+0x48` 出生置零即崩溃点形状）。
  r75 的 render-obj 候选被取代（旧报告留档不改）。
- CCB pack 公式 trace 实测：`ui32PackedCCBSizeU88=(arg5&0xff)<<8|(arg4&0xff)`。
- 边界：fabricated 恒走 legacy 分支，DDK2 离线不可驱动；
  下一步 = 活体 passthrough 非零 CCB create（需单独批准）。
- 证据：`mt-vgpu-guest/reports/r76-ghidra-ddk2-submissionbuf.md` +
  `r76-ccb-size-pack.jsonl`。
- 遗留：live 非零 CCB create + DDK2（待批）；真实 CCB 内容仍需绘制路径。

---

## 本次会话进展（AGENTS §9：Win 侧 + 反编译语料优先；用户指令）

- 用户要求：agent 提示优先参考 Windows 侧驱动实现与反编译工程。
  落为 §9 三条（语料→Win 包→Ghidra 工程复用不重跑；SHA 先对后用；
  语料是假设、执行是证据，与 §6.3 衔接）+ 检查单 +1 项；
  顺手把落点表 rNN 起点 r72→r77 订正。
- 纯文档改动，引用路径全存在。无 rNN 报告（非研究轮）。

---

## 本次会话进展（r77：DDK2 rsi 身份落定；离线语料）

- §9 流程首验：DDK2 rsi = 走完完整创建的 kicksync 对象；
  `+0x8/+0x18/+0x28` = 注册 server ctx / `_SyncPrimAlloc` /
  `SubmissionBufAlloctorCreate`（语料行号 L28320/28321/28331）；
  render-obj 候选彻底排除；`param_4` 是可选 OUT，传 0 正确。
- 推论：DDK2 唯一可达路径 = 活体非零 CCB create 走完三步门控
  （待批实验已精确到"对象已知、只差一次活体 create"）。
- 零硬件触碰，无新 trace。证据：`mt-vgpu-guest/reports/r77-ddk2-rsi-identity.md`。
- 遗留：live 非零 CCB create + DDK2（待批）；真实 CCB 内容仍需绘制路径。

---

## 本次会话进展（r78：活体非零 CCB create 仍走 legacy；批准的单次实验）

- 用户批准真机：passthrough 非零 CCB create（pack `0x0733` 活体生效），
  其后 0 SyncPrim 调用 → legacy 分支；DDK2 同址崩（dmesg `at 48` 写 fault 吻合）。
  根因：桥 `mt_pvr_device.h:143-145` 故意钉 `features+0x54<2`（bring-up 刻意选择）。
- 决策：update/DDK2 从"缺输入"转为"需桥特性开关"（改代码+重编+重载，
  单独立项单独批准）；check 侧即翻译器当前完整输入；T3 仍被 CCB 卡住。
- 会后零残留（23/34/38/0 全对，D 态 0，无新增 WARN）。
  证据：`mt-vgpu-guest/reports/r78-live-ccb-legacy-path.md` + jsonl。
- 遗留：特性开关立项（待批）；真实 CCB 内容仍需绘制路径。

---

## 本次会话进展（r79：vGPU 全路径梳理与方向评估；用户指令）

- 用户要完整梳理 + 方向 verdict：三线并行勘察 + 快照 §§3/8/9/10 对照。
- 结论：方向正确（翻译器是最小完备路径，accept-and-inspect 解耦关键），
  但结构性偏科——15 轮全在输入侧，T3（DM 格式）零进展，是最大风险；
  活体跑道基本见底（对象满/sealed/freeze），硬仗需新会话窗口。
- 建议顺序：T3 recon（离线语料）→ check-only 首帧设计（绕开 DDK2 的首胜路径）
  → 特性开关单独立项 → 会话更新窗口规划。
- 零硬件触碰。证据：`mt-vgpu-guest/reports/r79-path-review.md`。
- 遗留：按建议顺序推进（T3 recon 优先）；特性开关 + push 待批。
