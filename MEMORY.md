# MEMORY — 摩尔线程 vGPU 驱动适配

> **本文件只保留最新过程记录。**
> **当前状态的唯一权威快照见 [`PROGRESS-SNAPSHOT.md`](PROGRESS-SNAPSHOT.md)。**
> 2026-10-03 之前的全部逐轮记录原样归档于
> [`MEMORY-HISTORY-2026-10-01.md`](MEMORY-HISTORY-2026-10-01.md)（只读）。
> 两者冲突时以快照为准。

最后更新：2026-10-03（r45–r71 落地 + 文档清理：symbol_get 路线已弃用，PCI 直连；arena/cover/kick-inspect 在载；首次 RGX 执行 + 像素验证完成）
仓库：`/opt/ydyun-vdesktop-linux-driver`（分支 main）

---

## 本次会话进展（2026-10-03 文档清理：现状归一处，历史归档）

- `6f9b701`：`docs/PROGRESS.md` 加 Step 140（r43–r71 + bA38–bA43 摘要）；
  `mt-vgpu-guest/README.md` 顶加状态块；根 `README.md` 目录说明补
  `mt-vgpu-guest/` 并标注 `MTT-VGPU.md` 已被取代；`MTT-VGPU.md` 加状态注记。
- `fb38072`：`mt-vgpu-guest/README.md` 瘦身成入口（现状 + 运行态红线 +
  目录地图 + 门禁分层），r23–r41b 横幅堆栈与旧状态表原样移入
  `mt-vgpu-guest/HISTORY-2026-09.md`（434 行，只读）；
  新建 `docs/README.md` 文档地图（云电脑线 vs vGPU 双线 + 逐文件时效列）；
  归档头补到 `MTT-VGPU.md`、`PROTOCOL-NOTES.md`、`FIRMWARE-NOTES.md`、
  `HOST-REQUEST.md`（请求外部 Guest 包已不再执行）。
  11 个被引用路径逐个验存在；离线门禁 268 checks 绿；零硬件触碰。
- 遗留：`PROGRESS-SNAPSHOT.md` 头仍写 10-01/`41c4bd5`（bA43），§§1–11
  停在 bA43，只有 §12 有 10-03 活页刷新——需要一次快照刷新 pass，
  本轮只动 MEMORY，未动快照。

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
