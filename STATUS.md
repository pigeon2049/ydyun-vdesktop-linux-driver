# STATUS — 仓库当前状态总入口（2026-10-05）

> 新 agent 先读完本文件再动手。历史文件都有归档头；状态冲突时裁决顺序为
> `STATUS.md`（本文件）→ `PROGRESS-SNAPSHOT.md`（细节）→ `MEMORY.md`（过程）。
> 不要为“了解背景”通读归档文件——那是 token 黑洞。
> 机器可执行的完整约束见 [`AGENTS.md`](AGENTS.md)（落点、清理周期、红线、门禁）。

## 这是什么仓库（两条互不依赖的线）

| 线 | 目录 | 做什么 | 当前结论 |
|---|---|---|---|
| 云电脑 USB/画面 | `linux/`、`wayland/`、`docs/` | Debian 云电脑 USB 转发 + KDE Wayland 分辨率 | v0.2.52 可安装（见根 `README.md` 教程）；云端联调待真实会话 |
| S3000 vGPU Guest | `mt-vgpu-guest/` | 自研内核栈点亮 Moore Threads S3000 vGPU | bridge Oops 栈缺失、根因未定；r158 major 2 fabricated blit 到达 SubmitTransfer3，CCB GPU VA `0x8000f44000` / 长 `0x1200` 已关联到 PMR `0x500e` backing 并转储（39 非零，归属成立）；当前无驱动绑定、Guest/FW=2/1，仍不重复加载 bridge |

## vGPU 一句话现状

`mt_guest_probe` 与 `mt_pvr_bridge` 当前均未加载，S3000 `00:0e.0` 未绑定，设备 Guest/FW 状态为 2/1。r151 静态修正了 `pvr_mmap()` vmalloc 页转换/判空，以及 arena GPU 页表被 lazy VM 初始化覆盖和未初始化 close 泄漏；同时补了 `0x89:0xa` SubmitTransfer3 的 108B/4B packed wire ABI 描述与偏移断言，但 handler 未接入。r157 使用 opt-in `UMD_DRM_MAJOR=2` 使 fabricated 离屏 blit 到达 SubmitTransfer3，观测到 CCB GPU VA `0x8000f44000` 和长度 `0x1200`；shared-backing snapshot 中高占用 pool 的 CCB 归属尚未证明。shim 的 Submit/Wait 回包仍为伪造结果，trace 不证明 bridge 接受或 GPU 执行。最近一次 Oops 缺少 RIP/调用栈，尚不能证明缺陷根因，故不重复加载 bridge。UMD check-only kick 已在 legacy 与 `drm_major=2` 下经真实 DM2 空 marker 完成；真实绘制 CCB、update 数组语义和 TA/CDM 专属提交仍未验证。RGX 像素读回与 20 帧批量是已完成结果，不代表 `mt_live_3d_drm` 当前加载。
细节见 `PROGRESS-SNAPSHOT.md`，逐轮记录见 `MEMORY.md`（只留最新两节），
证据在 `mt-vgpu-guest/reports/r*.md`（索引见该目录 `reports/README.md`）。

## 活会话红线（先读这段，违反会毁掉数小时工作）

- 当前 probe/bridge 均未加载，设备 Guest/FW=2/1 且 PCI 未绑定；先定位上次 bridge 启动后的 kernel NULL dereference，暂不重复加载。
- 顶层 `make probe` / `make umd` 会先 rmmod 再 insmod，
  **在活会话上禁止直接使用**；只在可重建会话上跑。
- `timeout` 不得落在 bridge ioctl 临界区内（r67：device-mutex
  owner-death 泄漏只能靠重启恢复）。DMA 路径命令超时只做挂起探测，
  超时即停手、不堆任务。
- 一次只跑一个 live 实验模块，做完即卸（占着 trial_lock 会卡死下一个）。

## 门禁（`mt-vgpu-guest/` 下执行）

| 命令 | 含义 | 动硬件 |
|---|---|---|
| `make check-offline` | L1：269 Python（1 skip）+ 272 C RAM checks | 否 |
| `make check` | L1+L2：+ 内核 `W=1` 构建 + ABI 门禁 | 否（但依赖 gitignore 的 `build/` 产物，新 clone 会失败，见快照 §7） |
| `make kernel` | 全模块 `W=1` 构建 | 否 |
| `make probe` / `make umd` | L3/L4：加载模块跑探针 / 真实 UMD 八级阶梯 | **是**，且会重载模块——活会话上禁用 |

## 文档地图（只列活页；归档不在此列）

| 文件 | 性质 | 何时读 |
|---|---|---|
| `STATUS.md`（本文件） | 总入口 | 每次开工先读 |
| `PROGRESS-SNAPSHOT.md` | vGPU 权威快照（缺陷表、门禁、待办、教训） | 要动手前读对应章节 |
| `MEMORY.md` | 最新两节过程记录 | 要接上轮工作时读 |
| `mt-vgpu-guest/README.md` | 该目录入口：红线、目录、门禁命令 | 进该目录前读 |
| `mt-vgpu-guest/reports/README.md` | r 系列证据索引 | 找某轮证据时先查索引 |
| `docs/PROGRESS.md` | USB 线现状（短） | 动 USB/画面时读 |
| `docs/README.md` | 文档地图（含时效列） | 找文档时读 |
| 根 `README.md` | v0.2.52 安装教程 | 装机时读 |

归档（按需才读）：`MEMORY-HISTORY-2026-10-01.md`、
`mt-vgpu-guest/HISTORY-2026-09.md`、`docs/PROGRESS-HISTORY.md`、
`docs/MTT-VGPU-2026-09-22.md`，以及 `PROTOCOL/FIRMWARE-NOTES.md`
（顶层已有归档头）。

## 下一步（vGPU，按序）

1. **真实绘制 CCB**：check-only kick 已在 legacy 与 `drm_major=2` 路径经真实 DM2 空 marker 完成（r148–r149）。r157 已在 fabricated major 2 blit 中到达 `0x89:0xa`，得到 CCB GPU VA `0x8000f44000` / 长 `0x1200`；r158 已用 VA 台账将其关联到 PMR `0x500e` backing `0x500e000`+`0xf02` 并转储窗口字节（39 非零/FNV 已定，归属成立）；r160 已定位全部 39B 并对照 `SubmissionCmdGenerate` 语料（`+0x10`/`+0x28` 吻合）；r161 以离线 GDB 落定 `+0x40` 写入者为生成器头拷贝搬运的 job 计数器（活体栈兼证 TQJobSubmit 路径）；r162 换 producer 探针：fill CCB 与源面数无关，copy 路径 fabrication 下不可达；r163 确认 tq-perf 倒于同一 abort 点，非新 producer。producer 线暂止；候选转向步骤 2（待可重建会话）。shim 回包仍是假的，不能外推为真实提交。
2. **同步 update 语义**：源码已有 update 数组解析与完成后写回，但尚无活体验证；先从已核对 UMD 侧确定布局、可见性和完成条件。
3. **DDK2 TA/CDM 专属提交**：`0x82:0xC` / `0x81:0x5` 仍属 S4 真提交边界，空 marker 结果不能外推；待取得真实工作包与输入规约后再推进。
4. 长期：快照 §7 的门禁可复现（`build/` 产物入 git）与 in-tree 构建外移。
